#include "slang-autos/CompilationUtils.h"
#include "slang-autos/SignalAggregator.h"

#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/DeclaredType.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
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

namespace {

/// True when @p loc sits in the buffer whose content is @p site's file.
bool locationInSiteFile(const slang::SourceManager& sm, slang::SourceLocation loc,
                        const PortLookupSite& site) {
    if (!loc.buffer().valid()) return false;
    std::string_view text = sm.getSourceText(loc.buffer());
    // slang null-terminates its buffers; the analyzer's copy of the file is not.
    if (!text.empty() && text.back() == '\0') {
        text.remove_suffix(1);
    }
    return text == site.source_text;
}

/// The scope AUTO output is written into: the top instance whose body lives
/// in the site's file, or failing that the first top instance.
const Scope* findParentScope(Compilation& compilation, const PortLookupSite* site) {
    auto& root = compilation.getRoot();
    auto& sm = *compilation.getSourceManager();
    if (site) {
        for (auto* topInst : root.topInstances) {
            if (auto* syntax = topInst->body.getSyntax()) {
                if (locationInSiteFile(sm, syntax->sourceRange().start(), *site)) {
                    return &topInst->body;
                }
            }
        }
    }
    return root.topInstances.empty() ? nullptr : &root.topInstances[0]->body;
}

/// Walk up from an instance's own syntax to the enclosing instantiation
/// statement (`mod #(...) u_x (...);`), which is what analysis sites refer to.
const SyntaxNode* enclosingInstantiation(const SyntaxNode* node) {
    while (node && node->kind != SyntaxKind::HierarchyInstantiation) {
        node = node->parent;
    }
    return node;
}

/// Find an elaborated instance body for @p module_name under the top
/// instances. When a site is given and an instance was created from exactly
/// that site, prefer it so its parameter overrides are honoured; otherwise
/// any instance of the module will do.
const InstanceBodySymbol* findElaboratedBody(Compilation& compilation,
                                             const std::string& module_name,
                                             const PortLookupSite* site) {
    auto& root = compilation.getRoot();
    auto& sm = *compilation.getSourceManager();

    const InstanceBodySymbol* by_site = nullptr;
    const InstanceBodySymbol* by_name = nullptr;

    std::function<void(const Symbol&)> visit = [&](const Symbol& member) {
        if (by_site) return;
        if (auto* inst = member.as_if<InstanceSymbol>()) {
            if (inst->body.name != module_name) return;
            if (!by_name) by_name = &inst->body;
            if (site) {
                if (auto* stmt = enclosingInstantiation(inst->getSyntax())) {
                    auto loc = stmt->sourceRange().start();
                    if (loc.offset() == site->offset && locationInSiteFile(sm, loc, *site)) {
                        by_site = &inst->body;
                    }
                }
            }
        }
        else if (auto* arr = member.as_if<InstanceArraySymbol>()) {
            if (!arr->elements.empty()) visit(*arr->elements[0]);
        }
        else if (auto* gen = member.as_if<GenerateBlockSymbol>()) {
            for (auto& m : gen->members()) visit(m);
        }
        else if (auto* genArr = member.as_if<GenerateBlockArraySymbol>()) {
            for (auto* entry : genArr->entries) {
                if (entry) visit(*entry);
            }
        }
    };

    for (auto* topInst : root.topInstances) {
        for (auto& member : topInst->body.members()) {
            visit(member);
            if (by_site) return by_site;
        }
    }
    return by_name;
}

/// Locate the instantiation statement for @p site in the compilation's own
/// syntax trees. The analyzer works on an independently parsed tree, so the
/// node is matched by file content and offset rather than by pointer.
struct SiteFinder : public SyntaxVisitor<SiteFinder> {
    const slang::SourceManager& sm;
    const PortLookupSite& site;
    const HierarchyInstantiationSyntax* found = nullptr;

    SiteFinder(const slang::SourceManager& s, const PortLookupSite& p) : sm(s), site(p) {}

    void handle(const HierarchyInstantiationSyntax& node) {
        if (found) return;
        auto loc = node.sourceRange().start();
        if (loc.offset() == site.offset && locationInSiteFile(sm, loc, site)) {
            found = &node;
            return;
        }
        visitDefault(node);
    }
};

const HierarchyInstantiationSyntax* findSiteSyntax(Compilation& compilation,
                                                   const PortLookupSite& site) {
    auto& sm = *compilation.getSourceManager();
    for (auto& tree : compilation.getSyntaxTrees()) {
        SiteFinder finder(sm, site);
        tree->root().visit(finder);
        if (finder.found) return finder.found;
    }
    return nullptr;
}

/// Instantiate @p module_name on demand because elaboration produced no
/// instance of it (typically: only instantiated in a pruned generate branch).
/// A "virtual" instance is created: it is never added to any scope, but its
/// parent is set to the top module body so the site's parameter assignments
/// resolve exactly as they would in a live branch. Without a usable site the
/// definition's default parameters apply.
const InstanceBodySymbol* instantiateOnDemand(Compilation& compilation,
                                              const std::string& module_name,
                                              const PortLookupSite* site) {
    const Scope* scope = findParentScope(compilation, site);
    if (!scope) return nullptr;

    auto lookup = compilation.tryGetDefinition(module_name, *scope);
    if (!lookup.definition || lookup.definition->kind != SymbolKind::Definition) {
        return nullptr;
    }
    auto& def = lookup.definition->as<DefinitionSymbol>();

    const ParameterValueAssignmentSyntax* params = nullptr;
    slang::SourceLocation loc = def.location;
    if (site) {
        if (auto* syntax = findSiteSyntax(compilation, *site)) {
            params = syntax->parameters;
            loc = syntax->type.location();
        }
    }

    ASTContext context(*scope, LookupLocation::max);
    auto& inst = InstanceSymbol::createVirtual(context, loc, def, params);
    return &inst.body;
}

/// True if any identifier in the port's dimensions names a symbol private to
/// the child (a parameter or localparam of @p body) that the parent scope does
/// not also define. Such text is meaningless where the AUTO output is written,
/// so the resolved width must be used instead. A name the parent also defines
/// (a pass-through parameter) and macros are copied verbatim.
bool dimensionsReferenceChildScope(const PortSymbol& portSym, const InstanceBodySymbol& body,
                                   const Scope* parent) {
    const Symbol* internal = portSym.internalSymbol;
    if (!internal) return false;
    const DeclaredType* declType = internal->getDeclaredType();
    if (!declType) return false;
    const DataTypeSyntax* typeSyntax = declType->getTypeSyntax();
    if (!typeSyntax) return false;

    auto check = [&](const auto& dimensions) {
        for (size_t i = 0; i < dimensions.size(); ++i) {
            for (const auto& id : extractIdentifiersFromSyntax(*dimensions[i])) {
                if (body.find(id) && !(parent && parent->find(id))) return true;
            }
        }
        return false;
    };
    if (IntegerTypeSyntax::isKind(typeSyntax->kind)) {
        return check(typeSyntax->as<IntegerTypeSyntax>().dimensions);
    }
    if (typeSyntax->kind == SyntaxKind::ImplicitType) {
        return check(typeSyntax->as<ImplicitTypeSyntax>().dimensions);
    }
    return false;
}

} // anonymous namespace

std::vector<PortInfo> getModulePortsFromCompilation(
    slang::ast::Compilation& compilation,
    const std::string& module_name,
    DiagnosticCollector* diagnostics,
    StrictnessMode strictness,
    const PortLookupSite* site) {

    std::vector<PortInfo> ports;

    const Scope* parent_scope = findParentScope(compilation, site);
    const InstanceBodySymbol* found_body = findElaboratedBody(compilation, module_name, site);
    if (!found_body) {
        found_body = instantiateOnDemand(compilation, module_name, site);
    }

    if (!found_body) {
        if (diagnostics) {
            std::ostringstream msg;
            msg << "Module not found: " << module_name;
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

            // Try to extract original syntax (preserves parameters/macros), but
            // not when it names the child's own parameters: those don't exist
            // in the parent, so `[W-1:0]` would be copied into a scope where W
            // is undefined (or worse, means something else).
            if (!dimensionsReferenceChildScope(*portSym, *found_body, parent_scope)) {
                info.original_range_str =
                    extractOriginalDimensions(*portSym, *compilation.getSourceManager());
            }

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
