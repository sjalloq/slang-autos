#include "slang-autos/DotStarExpander.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/AllSyntax.h>

namespace slang_autos {

using namespace slang;
using namespace slang::syntax;
using namespace slang::ast;

DotStarExpander::DotStarExpander(
    Compilation& compilation,
    const DotStarExpanderOptions& opts)
    : compilation_(compilation)
    , options_(opts) {
}

namespace {

/// Find the column of a given offset in the source (0-based).
size_t columnOf(std::string_view source, size_t offset) {
    // Walk backwards to find the start of the line
    size_t line_start = offset;
    while (line_start > 0 && source[line_start - 1] != '\n') {
        --line_start;
    }
    return offset - line_start;
}

/// Process a single hierarchical instance, expanding .* if present.
/// Returns true if a replacement was generated.
bool processInstance(
    const HierarchyInstantiationSyntax& hier,
    const HierarchicalInstanceSyntax& inst,
    Compilation& compilation,
    std::string_view source,
    const DotStarExpanderOptions& options,
    std::vector<Replacement>& replacements,
    DiagnosticCollector& diagnostics) {

    // Find the WildcardPortConnection in this instance's connections
    const WildcardPortConnectionSyntax* wildcard = nullptr;

    // Also collect explicitly-named ports
    std::unordered_set<std::string> explicit_ports;

    for (size_t idx = 0; idx < inst.connections.size(); ++idx) {
        auto* conn = inst.connections[idx];
        if (conn->kind == SyntaxKind::WildcardPortConnection) {
            wildcard = &conn->as<WildcardPortConnectionSyntax>();
        } else if (conn->kind == SyntaxKind::NamedPortConnection) {
            auto& named = conn->as<NamedPortConnectionSyntax>();
            explicit_ports.insert(std::string(named.name.valueText()));
        }
    }

    if (!wildcard) return false;

    // Get the module name from the instantiation
    std::string module_name = std::string(hier.type.valueText());
    if (module_name.empty()) return false;

    // Get all ports of the instantiated module
    auto ports = getModulePortsFromCompilation(
        compilation, module_name, &diagnostics, options.strictness);

    if (ports.empty()) {
        return false;
    }

    // Filter out explicitly-connected ports
    std::vector<const PortInfo*> wildcard_ports;
    for (const auto& port : ports) {
        if (explicit_ports.find(port.name) == explicit_ports.end()) {
            wildcard_ports.push_back(&port);
        }
    }

    // Byte range of the .* tokens
    size_t dot_offset = wildcard->dot.location().offset();
    size_t star_end = wildcard->star.location().offset() + wildcard->star.rawText().size();

    if (wildcard_ports.empty()) {
        // All ports already explicitly connected - remove .* and surrounding comma/whitespace.
        // Expand replacement range to include the whole line containing .*
        // and one adjacent separator comma.
        size_t line_start = dot_offset;
        while (line_start > 0 && source[line_start - 1] != '\n') {
            --line_start;
        }
        size_t line_end = star_end;
        while (line_end < source.size() && source[line_end] != '\n') {
            ++line_end;
        }
        if (line_end < source.size()) ++line_end; // consume \n

        replacements.emplace_back(line_start, line_end, "",
                                  "remove empty .* for " + module_name);
        return true;
    }

    // Determine indentation from column of the .* dot token
    size_t indent_col = columnOf(source, dot_offset);
    std::string indent(indent_col, ' ');

    // Find the longest port name for alignment
    size_t max_name_len = 0;
    if (options.alignment) {
        for (const auto* port : wildcard_ports) {
            max_name_len = std::max(max_name_len, port->name.size());
        }
    }

    // Build the replacement text - just the expanded ports, no extra commas.
    // The SeparatedSyntaxList's separator commas in the source handle boundaries.
    std::ostringstream oss;
    for (size_t i = 0; i < wildcard_ports.size(); ++i) {
        const auto* port = wildcard_ports[i];
        oss << "." << port->name;
        if (options.alignment && max_name_len > 0) {
            size_t pad = max_name_len - port->name.size();
            oss << std::string(pad, ' ');
        }
        oss << "(" << port->name << ")";

        if (i + 1 < wildcard_ports.size()) {
            oss << ",\n" << indent;
        }
    }

    // Simple replacement: just swap .* for the expanded port list.
    // The .* sits at its own position in the source. Separator commas between
    // list elements are preserved in the source text around our replacement.
    replacements.emplace_back(dot_offset, star_end, oss.str(),
                              "expand .* for " + module_name);
    return true;
}

} // anonymous namespace

void DotStarExpander::analyze(
    const std::shared_ptr<SyntaxTree>& tree,
    std::string_view source_content) {

    replacements_.clear();
    expanded_count_ = 0;
    source_content_ = source_content;

    // Walk the syntax tree looking for module declarations
    auto& root = tree->root();

    // Helper to process members recursively (handles generate blocks)
    std::function<void(const SyntaxNode&)> visit = [&](const SyntaxNode& node) {
        if (node.kind == SyntaxKind::HierarchyInstantiation) {
            auto& hier = node.as<HierarchyInstantiationSyntax>();
            for (auto* instNode : hier.instances) {
                if (processInstance(hier, *instNode, compilation_,
                                    source_content_, options_,
                                    replacements_, diagnostics_)) {
                    ++expanded_count_;
                }
            }
            return; // Don't recurse into instance children
        }

        // Recurse into child nodes
        for (size_t i = 0; i < node.getChildCount(); ++i) {
            auto* child = node.childNode(i);
            if (child) {
                visit(*child);
            }
        }
    };

    visit(root);
}

} // namespace slang_autos
