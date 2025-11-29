#include "slang-autos/AutowireRewriter.h"

#include <sstream>
#include <algorithm>

#include <slang/parsing/Token.h>
#include <slang/syntax/SyntaxPrinter.h>
#include <slang/syntax/AllSyntax.h>

namespace slang_autos {

using namespace slang;
using namespace slang::syntax;
using namespace slang::parsing;

AutowireRewriter::AutowireRewriter(
    const SignalAggregator& signals,
    const std::set<std::string>& existing_decls,
    bool use_logic)
    : signals_(signals)
    , existing_decls_(existing_decls)
    , use_logic_(use_logic) {
}

void AutowireRewriter::handle(const ModuleDeclarationSyntax& module) {
    const MemberSyntax* autowire_marker_node = nullptr;
    const MemberSyntax* auto_block_start = nullptr;
    const MemberSyntax* auto_block_end = nullptr;
    std::vector<const MemberSyntax*> nodes_to_remove;
    bool in_auto_block = false;

    // First pass: find markers
    for (auto member : module.members) {
        if (hasAutowireMarker(*member)) {
            autowire_marker_node = member;
        }

        if (isAutoBlockStart(*member)) {
            auto_block_start = member;
            in_auto_block = true;
        }

        if (in_auto_block) {
            nodes_to_remove.push_back(member);
        }

        if (isAutoBlockEnd(*member)) {
            auto_block_end = member;
            in_auto_block = false;
            // Don't include the end marker node in removal - it's the first node AFTER the block
            if (!nodes_to_remove.empty()) {
                nodes_to_remove.pop_back();
            }
        }
    }

    // If no AUTOWIRE marker found, nothing to do
    if (!autowire_marker_node) {
        return;
    }

    // Detect indent from the autowire marker
    std::string indent = "    ";  // Default
    if (auto tok = autowire_marker_node->getFirstToken()) {
        for (const auto& trivia : tok.trivia()) {
            if (trivia.kind == TriviaKind::Whitespace) {
                auto text = trivia.getRawText();
                // Find the last newline and get whitespace after it
                auto nl_pos = text.rfind('\n');
                if (nl_pos != std::string_view::npos) {
                    indent = std::string(text.substr(nl_pos + 1));
                }
            }
        }
    }

    // Generate new declarations
    std::string decl_text = generateDeclarations(indent);

    if (decl_text.empty()) {
        // No wires to generate - just remove any existing auto block
        for (auto* node : nodes_to_remove) {
            remove(*node);
        }
        return;
    }

    // Parse the generated text into syntax nodes
    // The parsed result is a CompilationUnitSyntax containing our declarations
    auto& parsed_root = parse(decl_text);

    // Extract members from the parsed compilation unit
    if (parsed_root.kind != SyntaxKind::CompilationUnit) {
        return;  // Unexpected parse result
    }

    auto& comp_unit = parsed_root.as<CompilationUnitSyntax>();

    if (auto_block_start && auto_block_end) {
        // Re-expansion: remove old content and insert new
        for (auto* node : nodes_to_remove) {
            remove(*node);
        }
        // Insert each parsed member before the node that marks end of auto block
        for (auto* member : comp_unit.members) {
            insertBefore(*auto_block_end, *member);
        }
    } else {
        // Fresh expansion: insert new members after the autowire marker
        // Use insertBefore on the next node to maintain order
        // Find the node after the autowire marker
        const MemberSyntax* next_node = nullptr;
        bool found_marker = false;
        for (auto member : module.members) {
            if (found_marker) {
                next_node = member;
                break;
            }
            if (member == autowire_marker_node) {
                found_marker = true;
            }
        }

        if (next_node) {
            // Insert each member before the next node (maintains order)
            for (auto* member : comp_unit.members) {
                insertBefore(*next_node, *member);
            }
        } else {
            // No node after marker - use insertAtBack on module members
            for (auto* member : comp_unit.members) {
                insertAtBack(module.members, *member);
            }
        }
    }
}

bool AutowireRewriter::hasAutowireMarker(const SyntaxNode& node) const {
    return hasMarkerInTrivia(node, "/*AUTOWIRE*/");
}

bool AutowireRewriter::isAutoBlockStart(const SyntaxNode& node) const {
    return hasMarkerInTrivia(node, "// Beginning of automatic wires");
}

bool AutowireRewriter::isAutoBlockEnd(const SyntaxNode& node) const {
    return hasMarkerInTrivia(node, "// End of automatics");
}

bool AutowireRewriter::hasMarkerInTrivia(const SyntaxNode& node,
                                          std::string_view marker) const {
    auto tok = node.getFirstToken();
    if (!tok) {
        return false;
    }

    for (const auto& trivia : tok.trivia()) {
        if (trivia.kind == TriviaKind::BlockComment ||
            trivia.kind == TriviaKind::LineComment) {
            auto text = trivia.getRawText();
            if (text.find(marker) != std::string_view::npos) {
                return true;
            }
        }
    }

    return false;
}

std::string AutowireRewriter::generateDeclarations(std::string_view indent) const {
    // Get all nets driven by instances
    std::vector<NetInfo> driven_nets = signals_.getInstanceDrivenNets();

    // Filter out already-declared signals
    std::vector<NetInfo> to_declare;
    for (const auto& net : driven_nets) {
        if (existing_decls_.find(net.name) == existing_decls_.end()) {
            to_declare.push_back(net);
        }
    }

    if (to_declare.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "\n" << indent << "// Beginning of automatic wires\n";

    std::string type_str = use_logic_ ? "logic" : "wire";

    for (const auto& net : to_declare) {
        oss << indent << type_str;

        std::string range = net.getRangeStr();
        if (!range.empty()) {
            oss << " " << range;
        }

        oss << " " << net.name << ";\n";
    }

    // Add end marker as a localparam that will be removed later, but captures
    // the comment as leading trivia
    oss << indent << "// End of automatics\n";
    oss << indent << "localparam _SLANG_AUTOS_END_MARKER_ = 0;\n";

    return oss.str();
}

} // namespace slang_autos
