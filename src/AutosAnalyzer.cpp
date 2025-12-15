#include "slang-autos/AutosAnalyzer.h"
#include "slang-autos/Constants.h"
#include "slang-autos/CompilationUtils.h"
#include "slang-autos/TemplateMatcher.h"

#include <sstream>
#include <algorithm>
#include <iomanip>

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/AllSyntax.h>

namespace slang_autos {

using namespace slang;
using namespace slang::syntax;
using namespace slang::parsing;
using namespace slang::ast;

// ════════════════════════════════════════════════════════════════════════════
// Constructor
// ════════════════════════════════════════════════════════════════════════════

AutosAnalyzer::AutosAnalyzer(
    Compilation& compilation,
    const std::vector<AutoTemplate>& templates,
    const AutosAnalyzerOptions& options)
    : compilation_(compilation)
    , templates_(templates)
    , options_(options) {
}

// ════════════════════════════════════════════════════════════════════════════
// Main entry point
// ════════════════════════════════════════════════════════════════════════════

void AutosAnalyzer::analyze(const std::shared_ptr<SyntaxTree>& tree,
                            std::string_view source_content) {
    replacements_.clear();
    autoinst_count_ = 0;
    autologic_count_ = 0;
    autoports_count_ = 0;
    source_content_ = source_content;

    auto& root = tree->root();

    if (root.kind == SyntaxKind::CompilationUnit) {
        auto& cu = root.as<CompilationUnitSyntax>();
        for (auto* member : cu.members) {
            if (member->kind == SyntaxKind::ModuleDeclaration) {
                processModule(member->as<ModuleDeclarationSyntax>());
            }
        }
    } else if (root.kind == SyntaxKind::ModuleDeclaration) {
        processModule(root.as<ModuleDeclarationSyntax>());
    }
}

void AutosAnalyzer::processModule(const ModuleDeclarationSyntax& module) {
    CollectedInfo info = collectModuleInfo(module);

    if (info.autoinsts.empty() && !info.has_autologic && !info.has_autoports) {
        return;
    }

    resolvePortsAndSignals(info);
    generateReplacements(module, info);
}

// ════════════════════════════════════════════════════════════════════════════
// Phase 1: Collection - all positions from AST
// ════════════════════════════════════════════════════════════════════════════

AutosAnalyzer::CollectedInfo
AutosAnalyzer::collectModuleInfo(const ModuleDeclarationSyntax& module) {
    CollectedInfo info;
    bool in_autologic_block = false;

    for (auto* member : module.members) {
        // ─────────────────────────────────────────────────────────────────────
        // AUTOINST - find marker in port list, get close paren position
        // ─────────────────────────────────────────────────────────────────────
        if (hasMarker(*member, std::string(markers::AUTOINST))) {
            AutoInstInfo inst_info;
            inst_info.node = member;

            if (auto inst = extractInstanceInfo(*member)) {
                inst_info.module_type = inst->first;
                inst_info.instance_name = inst->second;

                // Get line number of instance for template lookup
                // (verilog-mode uses closest preceding template)
                auto& hier = member->as<HierarchyInstantiationSyntax>();
                size_t inst_offset = hier.type.location().offset();
                size_t inst_line = 1;
                for (size_t i = 0; i < inst_offset && i < source_content_.size(); ++i) {
                    if (source_content_[i] == '\n') ++inst_line;
                }
                inst_info.templ = findTemplate(inst_info.module_type, inst_line);

                // Get positions from AST
                // HierarchyInstantiationSyntax has: type, parameters, instances, semi
                // HierarchicalInstanceSyntax has: decl, openParen, connections, closeParen
                if (!hier.instances.empty()) {
                    auto& first_inst = *hier.instances[0];

                    // Close paren position from AST
                    inst_info.close_paren_pos = first_inst.closeParen.location().offset();

                    // Find AUTOINST marker in the instance (port list area)
                    if (auto pos = findMarkerInNode(first_inst, markers::AUTOINST)) {
                        inst_info.marker_end = pos->second;
                    }

                    // Collect manual ports (before the marker)
                    for (auto* conn : first_inst.connections) {
                        // Check if we've hit the marker
                        if (auto tok = conn->getFirstToken(); tok.valid()) {
                            if (hasMarkerInTokenTrivia(tok, std::string(markers::AUTOINST))) {
                                break;  // Stop collecting manual ports
                            }
                        }
                        // Extract port name from .port(signal) syntax
                        if (conn->kind == SyntaxKind::NamedPortConnection) {
                            auto& named = conn->as<NamedPortConnectionSyntax>();
                            inst_info.manual_ports.insert(std::string(named.name.valueText()));
                        }
                    }
                }

                if (inst_info.marker_end > 0 && inst_info.close_paren_pos > 0) {
                    info.autoinsts.push_back(inst_info);
                }
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // AUTOLOGIC marker - position from trivia
        // ─────────────────────────────────────────────────────────────────────
        if (auto tok = member->getFirstToken(); tok.valid()) {
            if (auto pos = findMarkerInTrivia(tok, markers::AUTOLOGIC)) {
                info.has_autologic = true;
                info.autologic.marker_end = pos->second;
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // AUTOLOGIC existing block - positions from AST
        // Use precise marker positions, not member sourceRange, because
        // END_AUTOMATICS may be trivia on the NEXT member (which we don't want to delete)
        // ─────────────────────────────────────────────────────────────────────
        if (auto pos = findMarkerInNode(*member, markers::BEGIN_AUTOLOGIC)) {
            in_autologic_block = true;
            info.autologic.has_existing_block = true;
            // Find start of line containing the marker
            info.autologic.block_start = pos->first;
        }

        if (in_autologic_block) {
            if (auto pos = findMarkerInNode(*member, markers::END_AUTOMATICS)) {
                in_autologic_block = false;
                // End position is after the marker
                info.autologic.block_end = pos->second;
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // User declarations
        // ─────────────────────────────────────────────────────────────────────
        if (!in_autologic_block) {
            if (auto name = extractDeclarationName(*member)) {
                info.existing_decls.insert(*name);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // AUTOPORTS - positions from AST
    // ─────────────────────────────────────────────────────────────────────────
    if (module.header->ports &&
        module.header->ports->kind == SyntaxKind::AnsiPortList) {

        auto& ansi = module.header->ports->as<AnsiPortListSyntax>();

        // Close paren position from token
        info.autoports.close_paren_pos = ansi.closeParen.location().offset();

        // Check for marker on close paren trivia
        if (auto pos = findMarkerInTrivia(ansi.closeParen, markers::AUTOPORTS)) {
            info.has_autoports = true;
            info.autoports.marker_end = pos->second;
        }

        // Scan ports for marker
        bool found_marker = false;
        for (auto* port : ansi.ports) {
            if (auto tok = port->getFirstToken(); tok.valid()) {
                if (auto pos = findMarkerInTrivia(tok, markers::AUTOPORTS)) {
                    info.has_autoports = true;
                    info.autoports.marker_end = pos->second;
                    found_marker = true;
                }
            }

            if (!found_marker) {
                // Port before marker - track name for filtering
                // TODO: Handle other ANSI port syntax kinds if needed (e.g., ExplicitAnsiPort).
                // Currently ImplicitAnsiPort covers most common cases including:
                //   - input clk
                //   - input wire clk
                //   - output logic [7:0] data
                if (port->kind == SyntaxKind::ImplicitAnsiPort) {
                    auto& implicit = port->as<ImplicitAnsiPortSyntax>();
                    std::string name = std::string(implicit.declarator->name.valueText());
                    info.autoports.existing_ports.insert(name);
                    info.existing_decls.insert(name);
                }
            }
        }
    }

    return info;
}

// ════════════════════════════════════════════════════════════════════════════
// AST Position Helpers
// ════════════════════════════════════════════════════════════════════════════

bool AutosAnalyzer::hasMarkerInTokenTrivia(Token tok, std::string_view marker) const {
    for (const auto& trivia : tok.trivia()) {
        if (trivia.getRawText().find(marker) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

std::optional<std::pair<size_t, size_t>>
AutosAnalyzer::findMarkerInTrivia(Token tok, std::string_view marker) const {
    // slang attaches trivia as "leading trivia" to the following token.
    // The trivia appears contiguously before the token in the source.
    // We calculate trivia positions by:
    //   1. Getting the token's location (first char of actual token text)
    //   2. Subtracting total trivia length to find where trivia starts
    //   3. Walking forward through trivia to find our marker
    //
    // Example: "  /*AUTOINST*/\n  .clk"
    //          ^                 ^
    //          trivia_start      token_loc (points to '.' in '.clk')
    size_t token_loc = tok.location().offset();

    // Calculate total trivia length to determine where each trivia starts
    size_t total_trivia_len = 0;
    for (const auto& trivia : tok.trivia()) {
        total_trivia_len += trivia.getRawText().length();
    }

    // Now walk through trivia, calculating each one's position
    size_t trivia_offset = token_loc - total_trivia_len;
    for (const auto& trivia : tok.trivia()) {
        auto raw = trivia.getRawText();
        auto pos = raw.find(marker);
        if (pos != std::string_view::npos) {
            size_t start = trivia_offset + pos;
            size_t end = start + marker.length();
            return std::make_pair(start, end);
        }
        trivia_offset += raw.length();
    }
    return std::nullopt;
}

bool AutosAnalyzer::hasMarker(const SyntaxNode& node, std::string_view marker) const {
    return node.toString().find(marker) != std::string::npos;
}

std::optional<std::pair<size_t, size_t>>
AutosAnalyzer::findMarkerInNode(const SyntaxNode& node, std::string_view marker) const {
    // Check all children (both nodes and tokens)
    for (size_t i = 0; i < node.getChildCount(); ++i) {
        // Try as token first
        if (auto tok = node.childToken(i); tok.valid()) {
            if (auto found = findMarkerInTrivia(tok, marker)) {
                return found;
            }
            // Also check token text itself (for block comments that aren't trivia)
            auto text = tok.rawText();
            auto pos = text.find(marker);
            if (pos != std::string_view::npos) {
                size_t start = tok.location().offset() + pos;
                return std::make_pair(start, start + marker.length());
            }
        }

        // Try as child node (recurse)
        if (auto* child = node.childNode(i)) {
            if (auto found = findMarkerInNode(*child, marker)) {
                return found;
            }
        }
    }

    return std::nullopt;
}

// ════════════════════════════════════════════════════════════════════════════
// Phase 2: Resolution
// ════════════════════════════════════════════════════════════════════════════

void AutosAnalyzer::resolvePortsAndSignals(CollectedInfo& info) {
    aggregator_ = SignalAggregator();

    for (auto& inst : info.autoinsts) {
        auto ports = getModulePorts(inst.module_type);
        if (ports.empty()) continue;

        auto connections = buildConnections(inst, ports);
        aggregator_.addFromInstance(inst.instance_name, connections, ports);
    }
}

std::vector<PortInfo> AutosAnalyzer::getModulePorts(const std::string& module_name) {
    return getModulePortsFromCompilation(
        compilation_, module_name, options_.diagnostics, options_.strictness);
}

std::vector<PortConnection> AutosAnalyzer::buildConnections(
    const AutoInstInfo& inst,
    const std::vector<PortInfo>& ports) {

    std::vector<PortConnection> connections;
    TemplateMatcher matcher(inst.templ, nullptr);
    matcher.setInstance(inst.instance_name);

    for (const auto& port : ports) {
        if (inst.manual_ports.count(port.name)) continue;

        PortConnection conn;
        conn.port_name = port.name;
        conn.direction = port.direction;

        auto match = matcher.matchPort(port);
        if (TemplateMatcher::isSpecialValue(match.signal_name)) {
            if (match.signal_name == "_") {
                conn.is_unconnected = true;
                conn.signal_expr = "";
            } else {
                conn.is_constant = true;
                conn.signal_expr = TemplateMatcher::formatSpecialValue(match.signal_name);
            }
        } else {
            conn.signal_expr = match.signal_name;
        }

        connections.push_back(conn);
    }

    return connections;
}

// ════════════════════════════════════════════════════════════════════════════
// Phase 3: Generate Replacements
// ════════════════════════════════════════════════════════════════════════════

void AutosAnalyzer::generateReplacements(
    const ModuleDeclarationSyntax& module,
    const CollectedInfo& info) {

    for (const auto& inst : info.autoinsts) {
        auto ports = getModulePorts(inst.module_type);
        if (!ports.empty()) {
            generateAutoInstReplacement(inst, ports);
        }
    }

    if (info.has_autologic) {
        generateAutologicReplacement(info);
    }

    if (info.has_autoports) {
        generateAutoportsReplacement(module, info);
    }
}

void AutosAnalyzer::generateAutoInstReplacement(
    const AutoInstInfo& inst,
    const std::vector<PortInfo>& ports) {

    // Count how many ports will be auto-generated (not manually connected)
    size_t auto_port_count = 0;
    for (const auto& port : ports) {
        if (!inst.manual_ports.count(port.name)) {
            ++auto_port_count;
        }
    }

    std::string port_text = generatePortConnections(inst, ports);

    // If there are manual ports AND auto ports to generate, check if we need
    // to add a comma between them. Look backwards from AUTOINST marker for the
    // last non-whitespace character - if it's not a comma, we need to add one.
    if (!inst.manual_ports.empty() && auto_port_count > 0) {
        size_t marker_start = inst.marker_end - markers::AUTOINST.length();
        bool needs_comma = true;

        // Search backwards for last non-whitespace character
        for (size_t i = marker_start; i > 0; --i) {
            char c = source_content_[i - 1];
            if (c == ',') {
                needs_comma = false;
                break;
            } else if (!std::isspace(static_cast<unsigned char>(c))) {
                // Found non-whitespace that isn't comma - need to add comma
                break;
            }
        }

        if (needs_comma) {
            port_text = "," + port_text;
        }
    }

    // Check if replacement would actually change the content
    if (inst.marker_end < inst.close_paren_pos &&
        inst.close_paren_pos <= source_content_.size()) {
        std::string_view original = source_content_.substr(
            inst.marker_end, inst.close_paren_pos - inst.marker_end);
        if (original == port_text) {
            // No change needed - already expanded
            return;
        }
    }

    // Replace from marker end to close paren
    replacements_.push_back({
        inst.marker_end,
        inst.close_paren_pos,
        port_text,
        "AUTOINST: " + inst.instance_name
    });

    ++autoinst_count_;
}

void AutosAnalyzer::generateAutologicReplacement(const CollectedInfo& info) {
    std::string decls = generateAutologicDecls(info.existing_decls);

    if (decls.empty() && !info.autologic.has_existing_block) {
        return;
    }

    if (info.autologic.has_existing_block) {
        // Re-expansion: replace existing block
        // Note: block_start points to start of "// Beginning...", so the
        // preceding whitespace is preserved - don't add leading indent
        std::ostringstream oss;
        // Note: block_start points to the "// Beginning" marker itself,
        // so any leading whitespace before it is preserved automatically.
        // block_end is right after "// End of automatics" text - the original
        // newline(s) after it are preserved, so we don't add one here.
        if (!decls.empty()) {
            oss << markers::BEGIN_AUTOLOGIC << "\n";
            oss << decls;
            oss << options_.indent << markers::END_AUTOMATICS;
        }
        // If decls empty, we remove the block entirely (empty replacement)

        replacements_.push_back({
            info.autologic.block_start,
            info.autologic.block_end,
            oss.str(),
            "AUTOLOGIC re-expansion"
        });
    } else {
        // Fresh expansion: insert after marker
        std::ostringstream oss;
        oss << "\n" << options_.indent << markers::BEGIN_AUTOLOGIC << "\n";
        oss << decls;
        oss << options_.indent << markers::END_AUTOMATICS;

        replacements_.push_back({
            info.autologic.marker_end,
            info.autologic.marker_end,
            oss.str(),
            "AUTOLOGIC expansion"
        });
    }

    ++autologic_count_;
}

void AutosAnalyzer::generateAutoportsReplacement(
    const ModuleDeclarationSyntax& module,
    const CollectedInfo& info) {

    auto inputs = aggregator_.getExternalInputNets();
    auto outputs = aggregator_.getExternalOutputNets();
    auto inouts = aggregator_.getInoutNets();

    // Filter existing
    auto filter = [&](std::vector<NetInfo>& nets) {
        nets.erase(std::remove_if(nets.begin(), nets.end(),
            [&](const NetInfo& n) { return info.autoports.existing_ports.count(n.name); }),
            nets.end());
    };
    filter(inputs);
    filter(outputs);
    filter(inouts);

    // Build replacement text (goes after marker, before close paren)
    std::ostringstream oss;

    bool prefer_original = preferOriginalSyntax();
    auto fmt = [prefer_original](const std::string& dir, const NetInfo& n) {
        std::ostringstream p;
        p << dir << " logic";
        if (!n.getRangeStr(prefer_original).empty()) p << " " << n.getRangeStr(prefer_original);
        p << " " << n.name;
        return p.str();
    };

    // Collect all ports in order: outputs, inouts, inputs
    std::vector<std::pair<std::string, NetInfo>> all_ports;
    for (const auto& n : outputs) all_ports.emplace_back("output", n);
    for (const auto& n : inouts) all_ports.emplace_back("inout", n);
    for (const auto& n : inputs) all_ports.emplace_back("input", n);

    // Generate port list with commas between items
    for (size_t i = 0; i < all_ports.size(); ++i) {
        const auto& [dir, net] = all_ports[i];
        oss << "\n    " << fmt(dir, net);
        if (i < all_ports.size() - 1) oss << ",";
    }

    if (!all_ports.empty()) {
        oss << "\n";
    }

    replacements_.push_back({
        info.autoports.marker_end,
        info.autoports.close_paren_pos,
        oss.str(),
        "AUTOPORTS"
    });

    ++autoports_count_;
}

// ════════════════════════════════════════════════════════════════════════════
// Text Generation
// ════════════════════════════════════════════════════════════════════════════

std::string AutosAnalyzer::generatePortConnections(
    const AutoInstInfo& inst,
    const std::vector<PortInfo>& ports) {

    std::string indent = detectIndent(*inst.node);
    // Port connections get one additional indent level
    std::string port_indent = indent + indent;

    TemplateMatcher matcher(inst.templ, nullptr);
    matcher.setInstance(inst.instance_name);

    // Filter to auto-connected ports
    std::vector<const PortInfo*> auto_ports;
    for (const auto& port : ports) {
        if (!inst.manual_ports.count(port.name)) {
            auto_ports.push_back(&port);
        }
    }

    std::ostringstream oss;

    if (auto_ports.empty()) {
        oss << "\n" << indent;
        return oss.str();
    }

    // Find max port name length for alignment
    size_t max_len = 0;
    if (options_.alignment) {
        for (const auto* p : auto_ports) {
            max_len = std::max(max_len, p->name.length());
        }
    }

    // Group by direction if needed
    std::vector<const PortInfo*> sorted_ports;
    if (options_.grouping == PortGrouping::Alphabetical) {
        sorted_ports = auto_ports;
        std::sort(sorted_ports.begin(), sorted_ports.end(),
            [](const PortInfo* a, const PortInfo* b) { return a->name < b->name; });
    } else {
        // ByDirection: outputs, inouts, inputs
        for (const auto* p : auto_ports) {
            if (p->direction == "output") sorted_ports.push_back(p);
        }
        for (const auto* p : auto_ports) {
            if (p->direction == "inout") sorted_ports.push_back(p);
        }
        for (const auto* p : auto_ports) {
            if (p->direction == "input") sorted_ports.push_back(p);
        }
    }

    oss << "\n";

    std::string current_dir;
    for (size_t i = 0; i < sorted_ports.size(); ++i) {
        const auto* port = sorted_ports[i];
        auto match = matcher.matchPort(*port);

        // Direction comment for grouping
        if (options_.grouping == PortGrouping::ByDirection && port->direction != current_dir) {
            current_dir = port->direction;
            std::string comment = (current_dir == "output") ? "Outputs" :
                                  (current_dir == "inout") ? "Inouts" : "Inputs";
            oss << port_indent << "// " << comment << "\n";
        }

        oss << port_indent;

        if (TemplateMatcher::isSpecialValue(match.signal_name) && match.signal_name == "_") {
            // Unconnected
            if (options_.alignment) {
                oss << "." << std::left << std::setw(static_cast<int>(max_len))
                    << port->name << " ()";
            } else {
                oss << "." << port->name << " ()";
            }
        } else {
            std::string signal = TemplateMatcher::isSpecialValue(match.signal_name)
                ? TemplateMatcher::formatSpecialValue(match.signal_name)
                : match.signal_name;

            // Apply width adaptation (slicing, padding, or unused signal)
            signal = adaptSignalWidth(signal, *port, match, inst.instance_name);

            if (options_.alignment) {
                oss << "." << std::left << std::setw(static_cast<int>(max_len))
                    << port->name << " (" << signal << ")";
            } else {
                oss << "." << port->name << " (" << signal << ")";
            }
        }

        if (i < sorted_ports.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }

    oss << indent;
    return oss.str();
}

std::string AutosAnalyzer::generateAutologicDecls(
    const std::set<std::string>& existing_decls) {

    auto nets = aggregator_.getInternalNets();
    const auto& unused_signals = aggregator_.getUnusedSignals();

    std::vector<NetInfo> to_declare;
    for (const auto& net : nets) {
        if (!existing_decls.count(net.name)) {
            to_declare.push_back(net);
        }
    }

    // Also add unused signals (for output width adaptation)
    for (const auto& unused : unused_signals) {
        if (!existing_decls.count(unused.name)) {
            to_declare.push_back(unused);
        }
    }

    if (to_declare.empty()) return "";

    bool prefer_original = preferOriginalSyntax();

    std::ostringstream oss;
    for (const auto& net : to_declare) {
        oss << options_.indent << "logic";
        if (!net.getRangeStr(prefer_original).empty()) {
            oss << " " << net.getRangeStr(prefer_original);
        }
        oss << " " << net.name << ";\n";
    }

    return oss.str();
}

std::string AutosAnalyzer::adaptSignalWidth(
    const std::string& signal,
    const PortInfo& port,
    const MatchResult& match,
    const std::string& instance_name) {

    // Rule 1: Templates win - if user provided explicit template, use as-is
    if (match.matched_rule != nullptr) {
        return signal;
    }

    // Rule 2: Special values (constants, unconnected) - no adaptation
    if (TemplateMatcher::isSpecialValue(match.signal_name)) {
        return signal;
    }

    // Rule 3: Look up aggregated width for this signal
    const NetInfo* net_info = aggregator_.getNetInfo(signal);
    if (!net_info) {
        // Signal not in aggregator - return unchanged
        return signal;
    }

    int aggregated_width = net_info->width;
    int port_width = port.width;

    // Rule 4: Equal widths - no adaptation needed
    if (port_width == aggregated_width) {
        return signal;
    }

    // Rule 5: Port narrower than signal - slice down
    if (port_width < aggregated_width) {
        if (port_width == 1) {
            // Single-bit: use [0] not [0:0]
            return signal + "[0]";
        } else {
            // Multi-bit: use [msb:0]
            return signal + "[" + std::to_string(port_width - 1) + ":0]";
        }
    }

    // Rule 6: Port wider than signal - pad/extend
    // port_width > aggregated_width
    if (port.direction == "input") {
        // Zero-pad inputs: {'0, signal}
        return "{\'0, " + signal + "}";
    } else if (port.direction == "output") {
        // Use unused signal for upper bits
        std::string unused_name = "unused_" + signal + "_" + instance_name;
        int unused_width = port_width - aggregated_width;
        aggregator_.addUnusedSignal(unused_name, unused_width);
        return "{" + unused_name + ", " + signal + "}";
    } else {
        // Inout: warn and return unchanged (ambiguous case)
        if (options_.diagnostics) {
            options_.diagnostics->addWarning(
                "Width mismatch on inout port '" + port.name + "': port is " +
                std::to_string(port_width) + "-bit but signal '" + signal +
                "' is " + std::to_string(aggregated_width) + "-bit. " +
                "Bidirectional width adaptation is ambiguous.");
        }
        return signal;
    }
}

std::string AutosAnalyzer::detectIndent(const SyntaxNode& node) const {
    std::string indent = options_.indent;

    if (auto tok = node.getFirstToken(); tok.valid()) {
        bool saw_newline = false;
        for (const auto& trivia : tok.trivia()) {
            if (trivia.kind == TriviaKind::EndOfLine) {
                saw_newline = true;
            } else if (trivia.kind == TriviaKind::Whitespace && saw_newline) {
                indent = std::string(trivia.getRawText());
                saw_newline = false;
            } else {
                saw_newline = false;
            }
        }
    }

    return indent;
}

// ════════════════════════════════════════════════════════════════════════════
// Other Helpers
// ════════════════════════════════════════════════════════════════════════════

std::optional<std::pair<std::string, std::string>>
AutosAnalyzer::extractInstanceInfo(const MemberSyntax& member) const {
    if (member.kind != SyntaxKind::HierarchyInstantiation) return std::nullopt;

    auto& hier = member.as<HierarchyInstantiationSyntax>();
    std::string module_type = std::string(hier.type.valueText());
    if (module_type.empty()) return std::nullopt;

    if (hier.instances.empty()) return std::nullopt;
    auto& first = *hier.instances[0];
    if (!first.decl) return std::nullopt;

    std::string inst_name = std::string(first.decl->name.valueText());
    return std::make_pair(module_type, inst_name);
}

std::optional<std::string>
AutosAnalyzer::extractDeclarationName(const MemberSyntax& member) const {
    if (member.kind == SyntaxKind::DataDeclaration) {
        auto& decl = member.as<DataDeclarationSyntax>();
        if (!decl.declarators.empty()) {
            return std::string(decl.declarators[0]->name.valueText());
        }
    }
    if (member.kind == SyntaxKind::NetDeclaration) {
        auto& decl = member.as<NetDeclarationSyntax>();
        if (!decl.declarators.empty()) {
            return std::string(decl.declarators[0]->name.valueText());
        }
    }
    return std::nullopt;
}

const AutoTemplate* AutosAnalyzer::findTemplate(const std::string& module_name,
                                                size_t before_line) const {
    // Find the closest preceding template for this module (verilog-mode semantics).
    // Templates must appear before the instance they apply to.
    const AutoTemplate* best = nullptr;
    size_t best_line = 0;

    for (const auto& t : templates_) {
        if (t.module_name == module_name &&
            t.line_number < before_line &&
            t.line_number > best_line) {
            best = &t;
            best_line = t.line_number;
        }
    }

    return best;
}

} // namespace slang_autos
