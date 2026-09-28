#include "slang-autos/CompilationUtils.h"

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/DeclaredType.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/text/SourceManager.h"

#include <functional>
#include <sstream>

namespace slang_autos {

using namespace slang::ast;
using namespace slang::syntax;

namespace {

/// Recursively extract all packed array dimensions from a type.
/// For [7:0][3:0], returns "[7:0][3:0]".
std::string extractPackedDimensions(const Type& type) {
    std::string result;

    // Walk through nested packed array types
    const Type* current = &type;
    while (current->isPackedArray()) {
        auto& packed = current->getCanonicalType().as<PackedArrayType>();
        auto range = packed.range;
        result += "[" + std::to_string(range.left) + ":" + std::to_string(range.right) + "]";
        current = &packed.elementType;
    }

    return result;
}

/// Recursively extract all unpacked array dimensions from a type.
/// For logic [7:0] data [3:0][1:0], returns " [3:0][1:0]" (note leading space).
std::string extractUnpackedDimensions(const Type& type) {
    std::string result;

    // Walk through nested unpacked array types
    const Type* current = &type;
    while (current->kind == SymbolKind::FixedSizeUnpackedArrayType) {
        auto& unpacked = current->getCanonicalType().as<FixedSizeUnpackedArrayType>();
        auto range = unpacked.range;
        result += " [" + std::to_string(range.left) + ":" + std::to_string(range.right) + "]";
        current = &unpacked.elementType;
    }

    return result;
}

/// Extract original source text for a syntax node, preserving macro references.
///
/// Every token is mapped back to the file-level range it was written in. For a
/// token that came from a macro expansion that is the range of the outermost
/// macro invocation (nested macros are followed up to the file), so a macro
/// that expands to several tokens, e.g. `WD -> (`N+1), is written out exactly
/// once. Text between consecutive ranges in the same buffer (whitespace,
/// operators) is copied verbatim from the source.
std::string extractOriginalSourceText(const SyntaxNode& node, const slang::SourceManager& sm) {
    // Fast path: nothing came from a macro, so the node's own text is exact.
    bool hasMacroTokens = false;
    for (auto it = node.tokens_begin(); it != node.tokens_end(); ++it) {
        auto token = *it;
        if (token.valid() && sm.isMacroLoc(token.location())) {
            hasMacroTokens = true;
            break;
        }
    }
    if (!hasMacroTokens) {
        return node.toString();
    }

    // Map a token to the range where it is written in a file buffer.
    auto fileRangeOf = [&](const slang::parsing::Token& token) -> slang::SourceRange {
        auto loc = token.location();
        if (!sm.isMacroLoc(loc)) {
            return {loc, loc + token.rawText().length()};
        }
        // Follow nested expansions outwards until the invocation sits in a file.
        auto range = sm.getExpansionRange(loc);
        while (sm.isMacroLoc(range.start())) {
            range = sm.getExpansionRange(range.start());
        }
        return range;
    };

    std::string result;
    bool have_last = false;
    slang::SourceRange last;

    for (auto it = node.tokens_begin(); it != node.tokens_end(); ++it) {
        auto token = *it;
        if (!token.valid()) continue;

        auto range = fileRangeOf(token);
        if (have_last && range == last) {
            continue;  // another token of the same macro invocation
        }

        std::string_view text = sm.getSourceText(range.start().buffer());
        size_t start = range.start().offset();
        size_t stop = range.end().offset();
        if (text.empty() || start > stop || stop > text.size()) {
            result += token.rawText();  // defensive fallback, keep going
            have_last = false;
            continue;
        }

        // Copy any gap (whitespace, punctuation) between the previous range
        // and this one when both sit in the same buffer.
        if (have_last && last.end().buffer() == range.start().buffer() &&
            last.end().offset() < start) {
            result += text.substr(last.end().offset(), start - last.end().offset());
        }

        result += text.substr(start, stop - start);
        last = range;
        have_last = true;
    }

    return result;
}

/// Extract original dimension syntax from a port symbol (preserves params/macros).
/// Returns empty string if syntax cannot be extracted.
std::string extractOriginalDimensions(const PortSymbol& portSym, const slang::SourceManager& sm) {
    // Try to get the internal symbol (the actual variable declaration)
    const Symbol* internal = portSym.internalSymbol;
    if (!internal) return "";

    // Get the declared type which has the original syntax
    const DeclaredType* declType = internal->getDeclaredType();
    if (!declType) return "";

    const DataTypeSyntax* typeSyntax = declType->getTypeSyntax();
    if (!typeSyntax) return "";

    // Extract dimensions based on the type syntax kind
    std::string result;

    // For IntegerType (logic, reg, bit, etc.) with dimensions
    if (IntegerTypeSyntax::isKind(typeSyntax->kind)) {
        auto& intType = typeSyntax->as<IntegerTypeSyntax>();
        for (size_t i = 0; i < intType.dimensions.size(); ++i) {
            result += extractOriginalSourceText(*intType.dimensions[i], sm);
        }
    }
    // For ImplicitType (just dimensions, no keyword)
    else if (typeSyntax->kind == SyntaxKind::ImplicitType) {
        auto& implType = typeSyntax->as<ImplicitTypeSyntax>();
        for (size_t i = 0; i < implType.dimensions.size(); ++i) {
            result += extractOriginalSourceText(*implType.dimensions[i], sm);
        }
    }

    return result;
}

} // anonymous namespace

std::vector<PortInfo> getModulePortsFromCompilation(
    slang::ast::Compilation& compilation,
    const std::string& module_name,
    DiagnosticCollector* diagnostics,
    StrictnessMode strictness) {

    std::vector<PortInfo> ports;

    auto& root = compilation.getRoot();
    const InstanceBodySymbol* found_body = nullptr;

    // Helper function to check a member for a matching module body.
    // Uses std::function to allow recursive calls for multi-dimensional arrays
    // and generate blocks.
    std::function<bool(const Symbol&)> checkMember = [&](const Symbol& member) -> bool {
        // Handle single instances
        if (auto* inst = member.as_if<InstanceSymbol>()) {
            if (inst->body.name == module_name) {
                found_body = &inst->body;
                return true;
            }
        }
        // Handle instance arrays (e.g., module_name inst[2:0] (...))
        // InstanceArraySymbol contains InstanceSymbol elements
        else if (auto* instArray = member.as_if<InstanceArraySymbol>()) {
            // Get the first element of the array to access the body
            if (!instArray->elements.empty()) {
                // Elements are InstanceSymbol or InstanceArraySymbol (for multi-dimensional)
                const Symbol* elem = instArray->elements[0];
                // Recursively check the element
                if (checkMember(*elem)) {
                    return true;
                }
            }
        }
        // Handle generate blocks (e.g., if/case/loop generate)
        else if (auto* genBlock = member.as_if<GenerateBlockSymbol>()) {
            // Recursively search members inside the generate block
            for (auto& m : genBlock->members()) {
                if (checkMember(m)) {
                    return true;
                }
            }
        }
        // Handle generate block arrays (from loop generate)
        else if (auto* genArray = member.as_if<GenerateBlockArraySymbol>()) {
            // Search all elements in the array
            for (auto* elem : genArray->entries) {
                if (elem && checkMember(*elem)) {
                    return true;
                }
            }
        }
        return false;
    };

    // Search for the module in compilation's top instances
    for (auto* topInst : root.topInstances) {
        for (auto& member : topInst->body.members()) {
            if (checkMember(member)) {
                break;
            }
        }
        if (found_body) break;
    }

    if (!found_body) {
        if (diagnostics) {
            // Build diagnostic message with debug info about what was searched
            std::ostringstream msg;
            msg << "Module not found: " << module_name;

            // In verbose mode, list what modules WERE found
            std::vector<std::string> found_modules;
            for (auto* topInst : root.topInstances) {
                for (auto& member : topInst->body.members()) {
                    if (auto* inst = member.as_if<InstanceSymbol>()) {
                        found_modules.push_back(std::string(inst->body.name));
                    } else if (auto* instArray = member.as_if<InstanceArraySymbol>()) {
                        // For instance arrays, indicate it's an array
                        if (!instArray->elements.empty()) {
                            if (auto* elem = instArray->elements[0]->as_if<InstanceSymbol>()) {
                                found_modules.push_back(std::string(elem->body.name) + " (array)");
                            }
                        }
                    }
                }
            }

            if (!found_modules.empty()) {
                msg << " (found: ";
                for (size_t i = 0; i < found_modules.size() && i < 5; ++i) {
                    if (i > 0) msg << ", ";
                    msg << found_modules[i];
                }
                if (found_modules.size() > 5) {
                    msg << ", ... (" << (found_modules.size() - 5) << " more)";
                }
                msg << ")";
            }

            if (strictness == StrictnessMode::Strict) {
                diagnostics->addError(msg.str());
            } else {
                diagnostics->addWarning(msg.str());
            }
        }
        return ports;
    }

    // Extract ports from the body's port list
    for (auto* port : found_body->getPortList()) {
        PortInfo info;
        info.name = std::string(port->name);

        // Empty port name indicates a parsing failure (e.g., undefined macros)
        if (info.name.empty()) {
            if (diagnostics) {
                diagnostics->addError(
                    "Port with empty name in module '" + module_name +
                    "' (likely caused by undefined macros in port declaration). "
                    "Ensure all required macros are defined via +define+ or include files.",
                    "", 0, "port_parse");
            }
            return {};  // Return empty - caller will handle the error
        }

        if (auto* portSym = port->as_if<PortSymbol>()) {
            switch (portSym->direction) {
                case ArgumentDirection::In:
                    info.direction = "input";
                    break;
                case ArgumentDirection::Out:
                    info.direction = "output";
                    break;
                case ArgumentDirection::InOut:
                    info.direction = "inout";
                    break;
                default:
                    info.direction = "input";
                    break;
            }

            auto& type = portSym->getType();

            // For unpacked arrays, we need to get the element type for packed dimensions
            // e.g., logic [7:0] data [3:0] has element type logic [7:0]
            const Type* elementType = &type;
            if (type.kind == SymbolKind::FixedSizeUnpackedArrayType) {
                // Walk down to find the non-unpacked element type
                while (elementType->kind == SymbolKind::FixedSizeUnpackedArrayType) {
                    elementType = &elementType->getCanonicalType().as<FixedSizeUnpackedArrayType>().elementType;
                }
                info.is_array = true;
                info.array_dims = extractUnpackedDimensions(type);
            }

            info.width = elementType->getBitWidth();

            // Try to extract original syntax (preserves parameters/macros)
            info.original_range_str = extractOriginalDimensions(*portSym, *compilation.getSourceManager());

            // Fallback: extract from resolved type (preserves multi-dimensional structure)
            if (elementType->isPackedArray()) {
                info.range_str = extractPackedDimensions(*elementType);
            } else if (info.width > 1) {
                info.range_str = "[" + std::to_string(info.width - 1) + ":0]";
            }
        }

        ports.push_back(info);
    }

    return ports;
}

} // namespace slang_autos
