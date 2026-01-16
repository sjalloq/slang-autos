#include "slang-autos/CompilationUtils.h"

#include "slang/ast/Compilation.h"
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
/// Iterates through all tokens in the node and reconstructs the original text,
/// replacing expanded macro tokens with their original macro invocations.
std::string extractOriginalSourceText(const SyntaxNode& node, const slang::SourceManager& sm) {
    std::string result;

    // Track whether we've seen any macro tokens
    bool hasMacroTokens = false;

    // First pass: check if any tokens are from macro expansions
    for (auto it = node.tokens_begin(); it != node.tokens_end(); ++it) {
        auto token = *it;
        if (token.valid() && sm.isMacroLoc(token.location())) {
            hasMacroTokens = true;
            break;
        }
    }

    // If no macro tokens, just use toString() for efficiency
    if (!hasMacroTokens) {
        return node.toString();
    }

    // Second pass: reconstruct text, replacing macro tokens with original source
    // We need to track our position in the original source to handle mixed macro/non-macro
    slang::BufferID currentBuffer;
    size_t lastEndOffset = 0;
    bool firstToken = true;

    for (auto it = node.tokens_begin(); it != node.tokens_end(); ++it) {
        auto token = *it;
        if (!token.valid()) continue;

        auto tokenLoc = token.location();

        if (sm.isMacroLoc(tokenLoc)) {
            // Token is from macro expansion - get the original invocation location
            auto expansionRange = sm.getExpansionRange(tokenLoc);
            auto expansionStart = expansionRange.start();
            auto expansionEnd = expansionRange.end();

            std::string_view sourceText = sm.getSourceText(expansionStart.buffer());
            if (sourceText.empty()) {
                // Fallback: use token's raw text
                result += token.rawText();
                continue;
            }

            size_t macroStart = expansionStart.offset();
            size_t macroEnd = expansionEnd.offset();

            // Safety check
            if (macroStart >= sourceText.size() || macroEnd > sourceText.size() || macroStart > macroEnd) {
                result += token.rawText();
                continue;
            }

            // If this is our first token or we're in a new buffer, just extract the macro
            if (firstToken || currentBuffer != expansionStart.buffer()) {
                result += sourceText.substr(macroStart, macroEnd - macroStart);
                currentBuffer = expansionStart.buffer();
                lastEndOffset = macroEnd;
                firstToken = false;
            } else {
                // Check if there's a gap between last position and this macro
                // (there shouldn't be in well-formed code, but handle it)
                if (macroStart > lastEndOffset) {
                    // There's non-macro text between - include it
                    result += sourceText.substr(lastEndOffset, macroStart - lastEndOffset);
                }
                result += sourceText.substr(macroStart, macroEnd - macroStart);
                lastEndOffset = macroEnd;
            }
        } else {
            // Non-macro token - use its location to extract from original source
            auto tokenBuffer = tokenLoc.buffer();
            std::string_view sourceText = sm.getSourceText(tokenBuffer);

            if (sourceText.empty()) {
                result += token.rawText();
                continue;
            }

            size_t tokenStart = tokenLoc.offset();
            size_t tokenLen = token.rawText().length();
            size_t tokenEnd = tokenStart + tokenLen;

            // Safety check
            if (tokenStart >= sourceText.size() || tokenEnd > sourceText.size()) {
                result += token.rawText();
                continue;
            }

            if (firstToken || currentBuffer != tokenBuffer) {
                result += sourceText.substr(tokenStart, tokenLen);
                currentBuffer = tokenBuffer;
                lastEndOffset = tokenEnd;
                firstToken = false;
            } else {
                // Same buffer - check for gap (whitespace, etc.)
                if (tokenStart > lastEndOffset && currentBuffer == tokenBuffer) {
                    // Include any text between tokens (like whitespace within the expression)
                    result += sourceText.substr(lastEndOffset, tokenStart - lastEndOffset);
                }
                result += sourceText.substr(tokenStart, tokenLen);
                lastEndOffset = tokenEnd;
            }
        }
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
    // Uses std::function to allow recursive calls for multi-dimensional arrays.
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
