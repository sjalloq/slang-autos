#include "slang-autos/AutosRewriter.h"
#include "slang-autos/TemplateMatcher.h"

#include <sstream>
#include <algorithm>

#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/types/Type.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/parsing/Token.h>

namespace slang_autos {

using namespace slang;
using namespace slang::syntax;
using namespace slang::parsing;
using namespace slang::ast;

// ════════════════════════════════════════════════════════════════════════════
// Constructor
// ════════════════════════════════════════════════════════════════════════════

AutosRewriter::AutosRewriter(
    Compilation& compilation,
    const std::vector<AutoTemplate>& templates,
    const AutosRewriterOptions& options)
    : compilation_(compilation)
    , templates_(templates)
    , options_(options) {
}

// ════════════════════════════════════════════════════════════════════════════
// Main handler - called for each module
// ════════════════════════════════════════════════════════════════════════════

void AutosRewriter::handle(const ModuleDeclarationSyntax& module) {

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 1: COLLECT
    // ═══════════════════════════════════════════════════════════════════════
    CollectedInfo info = collectModuleInfo(module);


    // If no AUTO markers found, nothing to do
    if (info.autoinsts.empty() && !info.autowire_marker && !info.autoreg_marker) {
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 2: RESOLVE
    // ═══════════════════════════════════════════════════════════════════════
    resolvePortsAndSignals(info);

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 3: GENERATE & QUEUE
    // ═══════════════════════════════════════════════════════════════════════

    // Queue AUTOINST expansions
    for (const auto& inst : info.autoinsts) {
        auto ports = getModulePorts(inst.module_type);
        if (!ports.empty()) {
            queueAutoInstExpansion(inst, ports);
        }
    }

    // Queue AUTOWIRE expansion
    if (info.autowire_marker) {
        queueAutowireExpansion(info);
    }

    // Queue AUTOREG expansion
    if (info.autoreg_marker) {
        queueAutoregExpansion(info);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Phase 1: Collection
// ════════════════════════════════════════════════════════════════════════════

AutosRewriter::CollectedInfo
AutosRewriter::collectModuleInfo(const ModuleDeclarationSyntax& module) {
    CollectedInfo info;
    bool in_autowire_block = false;
    bool in_autoreg_block = false;
    bool need_autowire_next = false;
    bool need_autoreg_next = false;

    for (auto* member : module.members) {
        // Track next node after markers
        if (need_autowire_next && !info.autowire_next) {
            info.autowire_next = member;
            need_autowire_next = false;
        }
        if (need_autoreg_next && !info.autoreg_next) {
            info.autoreg_next = member;
            need_autoreg_next = false;
        }

        // Check for AUTOINST marker
        if (hasMarker(*member, "/*AUTOINST*/")) {
            AutoInstInfo inst_info;
            inst_info.node = member;

            // Extract module type and instance name from the syntax
            if (auto inst = extractInstanceInfo(*member)) {
                inst_info.module_type = inst->first;
                inst_info.instance_name = inst->second;
                inst_info.templ = findTemplate(inst_info.module_type);
                info.autoinsts.push_back(inst_info);
            }
        }

        // Check for AUTOWIRE marker
        if (hasMarker(*member, "/*AUTOWIRE*/")) {
            info.autowire_marker = member;
            need_autowire_next = true;  // Next iteration will capture the next node
        }

        // Check for AUTOREG marker
        if (hasMarker(*member, "/*AUTOREG*/")) {
            info.autoreg_marker = member;
            need_autoreg_next = true;
        }

        // Track existing auto blocks (for re-expansion)
        if (hasMarker(*member, "// Beginning of automatic wires")) {
            in_autowire_block = true;
        }
        if (hasMarker(*member, "// Beginning of automatic regs")) {
            in_autoreg_block = true;
        }

        if (in_autowire_block) {
            info.autowire_block.push_back(member);
        }
        if (in_autoreg_block) {
            info.autoreg_block.push_back(member);
        }

        if (hasMarker(*member, "// End of automatics")) {
            if (in_autowire_block) {
                in_autowire_block = false;
                info.autowire_block_end = member;
                // Remove the end marker from the block
                if (!info.autowire_block.empty()) {
                    info.autowire_block.pop_back();
                }
            }
            if (in_autoreg_block) {
                in_autoreg_block = false;
                info.autoreg_block_end = member;
                if (!info.autoreg_block.empty()) {
                    info.autoreg_block.pop_back();
                }
            }
        }

        // Collect user declarations (excluding auto blocks)
        if (!in_autowire_block && !in_autoreg_block) {
            if (auto name = extractDeclarationName(*member)) {
                info.existing_decls.insert(*name);
            }
        }
    }

    return info;
}

// ════════════════════════════════════════════════════════════════════════════
// Phase 2: Resolution
// ════════════════════════════════════════════════════════════════════════════

void AutosRewriter::resolvePortsAndSignals(CollectedInfo& info) {
    aggregator_ = SignalAggregator();  // Clear

    for (auto& inst : info.autoinsts) {
        auto ports = getModulePorts(inst.module_type);
        if (ports.empty()) {
            continue;  // Module not found
        }

        auto connections = buildConnections(inst, ports);
        aggregator_.addFromInstance(inst.instance_name, connections, ports);
    }
}

std::vector<PortInfo> AutosRewriter::getModulePorts(const std::string& module_name) {
    std::vector<PortInfo> ports;

    auto& root = compilation_.getRoot();
    const InstanceBodySymbol* found_body = nullptr;

    // Search for the module in compilation
    for (auto* topInst : root.topInstances) {
        for (auto& member : topInst->body.members()) {
            if (auto* inst = member.as_if<InstanceSymbol>()) {
                if (inst->body.name == module_name) {
                    found_body = &inst->body;
                    break;
                }
            }
        }
        if (found_body) break;
    }

    if (!found_body) {
        // Report diagnostic if configured
        if (options_.diagnostics) {
            if (options_.strictness == StrictnessMode::Strict) {
                options_.diagnostics->addError("Module not found: " + module_name);
            } else {
                options_.diagnostics->addWarning("Module not found: " + module_name);
            }
        }
        return ports;
    }

    // Extract ports
    for (auto* port : found_body->getPortList()) {
        PortInfo info;
        info.name = std::string(port->name);

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
            info.width = type.getBitWidth();

            if (type.isPackedArray()) {
                auto& packed = type.getCanonicalType().as<PackedArrayType>();
                auto range = packed.range;
                info.range_str = "[" + std::to_string(range.left) + ":" +
                                std::to_string(range.right) + "]";
            } else if (info.width > 1) {
                info.range_str = "[" + std::to_string(info.width - 1) + ":0]";
            }
        }

        ports.push_back(info);
    }

    return ports;
}

std::vector<PortConnection> AutosRewriter::buildConnections(
    const AutoInstInfo& inst,
    const std::vector<PortInfo>& ports) {

    std::vector<PortConnection> connections;

    // Create template matcher for @ substitution
    TemplateMatcher matcher(inst.templ, nullptr);
    matcher.setInstance(inst.instance_name);

    for (const auto& port : ports) {
        // Skip manually connected ports
        if (inst.manual_ports.count(port.name)) {
            continue;
        }

        PortConnection conn;
        conn.port_name = port.name;
        conn.direction = port.direction;

        // Use TemplateMatcher for proper @ substitution
        auto match_result = matcher.matchPort(port);
        std::string signal_name = match_result.signal_name;

        // Check for constants/disconnects
        if (TemplateMatcher::isSpecialValue(signal_name)) {
            if (signal_name == "_") {
                conn.is_unconnected = true;
                conn.is_constant = false;
                conn.signal_expr = "";
            } else {
                conn.is_constant = true;
                conn.is_unconnected = false;
                conn.signal_expr = TemplateMatcher::formatSpecialValue(signal_name);
            }
        } else {
            conn.is_constant = false;
            conn.is_unconnected = false;
            conn.signal_expr = signal_name;
        }

        connections.push_back(conn);
    }

    return connections;
}

// ════════════════════════════════════════════════════════════════════════════
// Phase 3: Generation and queuing
// ════════════════════════════════════════════════════════════════════════════

void AutosRewriter::queueAutoInstExpansion(
    const AutoInstInfo& inst,
    const std::vector<PortInfo>& ports) {

    // Generate the complete instance text with expanded port list
    std::string full_inst = generateFullInstanceText(inst, ports);
    if (full_inst.empty()) {
        return;
    }

    // Wrap in a module so it parses correctly
    std::string wrapper = "module _wrapper_;\n" + full_inst + "\nendmodule\n";
    auto& parsed = parse(wrapper);

    // Handle different parse results - parse() may return ModuleDeclaration directly
    // or wrap in CompilationUnit depending on slang version/configuration
    const ModuleDeclarationSyntax* mod_decl = nullptr;

    if (parsed.kind == SyntaxKind::ModuleDeclaration) {
        mod_decl = &parsed.as<ModuleDeclarationSyntax>();
    } else if (parsed.kind == SyntaxKind::CompilationUnit) {
        auto& comp_unit = parsed.as<CompilationUnitSyntax>();
        for (auto* member : comp_unit.members) {
            if (member->kind == SyntaxKind::ModuleDeclaration) {
                mod_decl = &member->as<ModuleDeclarationSyntax>();
                break;
            }
        }
    }

    if (!mod_decl) {
        return;
    }

    // Find the HierarchyInstantiation in the module
    for (auto* mod_member : mod_decl->members) {
        if (mod_member->kind == SyntaxKind::HierarchyInstantiation) {
            // Replace the original instance with the expanded one
            replace(*inst.node, *mod_member);
            return;
        }
    }
}

void AutosRewriter::queueAutowireExpansion(const CollectedInfo& info) {

    // Remove old auto block if exists
    for (auto* node : info.autowire_block) {
        remove(*node);
    }

    // Generate new declarations
    std::string decl_text = generateAutowireText(info.existing_decls);
    if (decl_text.empty()) {
        return;
    }

    // Parse into syntax nodes (wrap in module)
    std::string wrapper = "module _wrapper_;\n" + decl_text + "\nendmodule\n";
    auto& parsed = parse(wrapper);

    // Handle different parse results
    const ModuleDeclarationSyntax* mod_decl = nullptr;

    if (parsed.kind == SyntaxKind::ModuleDeclaration) {
        mod_decl = &parsed.as<ModuleDeclarationSyntax>();
    } else if (parsed.kind == SyntaxKind::CompilationUnit) {
        auto& comp_unit = parsed.as<CompilationUnitSyntax>();
        for (auto* member : comp_unit.members) {
            if (member->kind == SyntaxKind::ModuleDeclaration) {
                mod_decl = &member->as<ModuleDeclarationSyntax>();
                break;
            }
        }
    }

    if (!mod_decl) {
        return;
    }

    // Find the module in the parsed result
    std::vector<MemberSyntax*> decl_members;
    for (auto* mod_member : mod_decl->members) {
        decl_members.push_back(const_cast<MemberSyntax*>(mod_member));
    }

    if (decl_members.empty()) {
        return;
    }

    // Find where to insert
    const MemberSyntax* insert_point = nullptr;


    if (info.autowire_block_end) {
        // Re-expansion: insert before the end marker (which has the "End of automatics" comment)
        insert_point = info.autowire_block_end;
    } else if (info.autowire_next) {
        // Fresh expansion: insert before the next node after the marker
        insert_point = info.autowire_next;
    }

    if (insert_point) {
        // Insert all declarations before the insertion point
        for (auto* decl : decl_members) {
            insertBefore(*insert_point, *decl);
        }
    } else {
        // No insertion point found (autowire marker is last node)
        // Insert after the marker instead
        if (info.autowire_marker) {
            // Insert in reverse order since insertAfter puts each item right after the marker
            for (auto it = decl_members.rbegin(); it != decl_members.rend(); ++it) {
                insertAfter(*info.autowire_marker, **it);
            }
        }
    }
}

void AutosRewriter::queueAutoregExpansion(const CollectedInfo& /* info */) {
    // Similar to AUTOWIRE but for registers
    // TODO: Implement when needed
}

// ════════════════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════════════════

bool AutosRewriter::hasMarker(const SyntaxNode& node, std::string_view marker) const {
    // For AUTOINST markers inside HierarchyInstantiation, we need to check
    // all tokens in the node, not just the first one
    // Use toString() to get the full text representation and search for marker
    std::string node_text = node.toString();
    return node_text.find(marker) != std::string::npos;
}

std::optional<std::pair<std::string, std::string>>
AutosRewriter::extractInstanceInfo(const MemberSyntax& member) const {
    // Check if this is a hierarchy instantiation
    if (member.kind != SyntaxKind::HierarchyInstantiation) {
        return std::nullopt;
    }

    auto& hier = member.as<HierarchyInstantiationSyntax>();

    // Get module type from the type token (it's a Token, not a SyntaxNode)
    std::string module_type = std::string(hier.type.valueText());
    if (module_type.empty()) {
        return std::nullopt;
    }

    // Get instance name from first instance
    if (hier.instances.empty()) {
        return std::nullopt;
    }

    auto& first_inst = *hier.instances[0];
    if (!first_inst.decl) {
        return std::nullopt;
    }
    std::string instance_name = std::string(first_inst.decl->name.valueText());

    return std::make_pair(module_type, instance_name);
}

std::optional<std::string>
AutosRewriter::extractDeclarationName(const MemberSyntax& member) const {
    // Handle data declarations (wire, logic, reg, etc.)
    if (member.kind == SyntaxKind::DataDeclaration) {
        auto& decl = member.as<DataDeclarationSyntax>();
        if (!decl.declarators.empty()) {
            auto& first = *decl.declarators[0];
            return std::string(first.name.valueText());
        }
    }

    // Handle net declarations
    if (member.kind == SyntaxKind::NetDeclaration) {
        auto& decl = member.as<NetDeclarationSyntax>();
        if (!decl.declarators.empty()) {
            auto& first = *decl.declarators[0];
            return std::string(first.name.valueText());
        }
    }

    return std::nullopt;
}

const AutoTemplate* AutosRewriter::findTemplate(const std::string& module_name) const {
    for (const auto& tmpl : templates_) {
        if (tmpl.module_name == module_name) {
            return &tmpl;
        }
    }
    return nullptr;
}

std::string AutosRewriter::generateAutoInstText(
    const AutoInstInfo& inst,
    const std::vector<PortInfo>& ports) {

    std::ostringstream oss;
    std::string indent = options_.indent;

    // Group ports by direction
    std::vector<const PortInfo*> outputs, inputs, inouts;
    for (const auto& port : ports) {
        if (inst.manual_ports.count(port.name)) {
            continue;  // Skip manual ports
        }

        if (port.direction == "output") {
            outputs.push_back(&port);
        } else if (port.direction == "input") {
            inputs.push_back(&port);
        } else if (port.direction == "inout") {
            inouts.push_back(&port);
        }
    }

    // Collect all ports in order
    std::vector<std::pair<const PortInfo*, std::string>> all_ports;
    auto addGroup = [&](const std::vector<const PortInfo*>& group) {
        for (const auto* port : group) {
            std::string signal = port->name;
            // Apply template if available
            if (inst.templ) {
                for (const auto& rule : inst.templ->rules) {
                    if (rule.port_pattern == port->name) {
                        signal = rule.signal_expr;
                        break;
                    }
                }
            }
            all_ports.push_back({port, signal});
        }
    };

    addGroup(outputs);
    addGroup(inouts);
    addGroup(inputs);

    // Write port connections
    for (size_t i = 0; i < all_ports.size(); ++i) {
        const auto& [port, signal] = all_ports[i];
        oss << indent << "." << port->name << "(" << signal << ")";
        if (i < all_ports.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }

    return oss.str();
}

std::string AutosRewriter::generateFullInstanceText(
    const AutoInstInfo& inst,
    const std::vector<PortInfo>& ports) {

    std::ostringstream oss;
    std::string indent = options_.indent;

    // Create template matcher for @ substitution
    TemplateMatcher matcher(inst.templ, nullptr);
    matcher.setInstance(inst.instance_name);

    // Module type and instance name
    oss << inst.module_type << " " << inst.instance_name << " (/*AUTOINST*/\n";

    // Group ports by direction
    std::vector<const PortInfo*> outputs, inputs, inouts;
    for (const auto& port : ports) {
        if (inst.manual_ports.count(port.name)) {
            continue;  // Skip manual ports
        }

        if (port.direction == "output") {
            outputs.push_back(&port);
        } else if (port.direction == "input") {
            inputs.push_back(&port);
        } else if (port.direction == "inout") {
            inouts.push_back(&port);
        }
    }

    // Collect all ports with their signal names and group comments
    struct PortEntry {
        const PortInfo* port;
        std::string signal;
        std::string group_comment;
        bool is_unconnected;
        bool is_constant;
    };
    std::vector<PortEntry> all_ports;

    auto addGroup = [&](const std::vector<const PortInfo*>& group,
                        const std::string& comment) {
        if (group.empty()) return;
        bool first = true;
        for (const auto* port : group) {
            // Use TemplateMatcher to apply template with @ substitution
            auto match_result = matcher.matchPort(*port);

            PortEntry entry;
            entry.port = port;
            entry.group_comment = first ? comment : "";
            entry.is_unconnected = false;
            entry.is_constant = false;

            if (TemplateMatcher::isSpecialValue(match_result.signal_name)) {
                if (match_result.signal_name == "_") {
                    entry.is_unconnected = true;
                    entry.signal = "";
                } else {
                    entry.is_constant = true;
                    entry.signal = TemplateMatcher::formatSpecialValue(match_result.signal_name);
                }
            } else {
                entry.signal = match_result.signal_name;
            }

            all_ports.push_back(entry);
            first = false;
        }
    };

    addGroup(outputs, "Outputs");
    addGroup(inouts, "Inouts");
    addGroup(inputs, "Inputs");

    // Write port connections
    for (size_t i = 0; i < all_ports.size(); ++i) {
        const auto& entry = all_ports[i];

        // Add group comment if present
        if (!entry.group_comment.empty()) {
            oss << indent << "// " << entry.group_comment << "\n";
        }

        // Format the port connection
        if (entry.is_unconnected) {
            oss << indent << "." << entry.port->name << "()";
        } else {
            oss << indent << "." << entry.port->name << "(" << entry.signal << ")";
        }

        if (i < all_ports.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }

    oss << ");";

    return oss.str();
}

std::string AutosRewriter::generateAutowireText(
    const std::set<std::string>& existing_decls) {

    auto driven_nets = aggregator_.getInstanceDrivenNets();

    // Filter out already-declared signals
    std::vector<NetInfo> to_declare;
    for (const auto& net : driven_nets) {
        if (existing_decls.find(net.name) == existing_decls.end()) {
            to_declare.push_back(net);
        }
    }

    if (to_declare.empty()) {
        return "";
    }

    std::ostringstream oss;
    std::string indent = options_.indent;
    std::string type_str = options_.use_logic ? "logic" : "wire";

    oss << "\n" << indent << "// Beginning of automatic wires\n";

    for (size_t i = 0; i < to_declare.size(); ++i) {
        const auto& net = to_declare[i];
        oss << indent << type_str;

        std::string range = net.getRangeStr();
        if (!range.empty()) {
            oss << " " << range;
        }

        oss << " " << net.name << ";";

        if (i == to_declare.size() - 1) {
            oss << " // End of automatics";
        }
        oss << "\n";
    }

    // Add dummy marker for trivia preservation
    oss << indent << "localparam _SLANG_AUTOS_END_MARKER_ = 0;\n";

    return oss.str();
}

std::string AutosRewriter::detectIndent(const SyntaxNode& node) const {
    std::string indent = options_.indent;  // Default

    if (auto tok = node.getFirstToken()) {
        for (const auto& trivia : tok.trivia()) {
            if (trivia.kind == TriviaKind::Whitespace) {
                auto text = trivia.getRawText();
                auto nl_pos = text.rfind('\n');
                if (nl_pos != std::string_view::npos) {
                    indent = std::string(text.substr(nl_pos + 1));
                }
            }
        }
    }

    return indent;
}

} // namespace slang_autos
