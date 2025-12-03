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
    if (info.autoinsts.empty() && !info.autowire_marker && !info.autoreg_marker && !info.has_autoports) {
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

    // Queue AUTOPORTS expansion (ANSI-style port list)
    if (info.has_autoports) {
        queueAutoportsExpansion(module, info);
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
    bool need_autoreg_next = false;
    bool need_autowire_after = false;  // Track node after autowire block (for re-expansion insert point)
    bool need_autoreg_after = false;   // Track node after autoreg block

    for (auto* member : module.members) {
        // Track next node after AUTOREG marker (still needed for AUTOREG)
        if (need_autoreg_next && !info.autoreg_next) {
            info.autoreg_next = member;
            need_autoreg_next = false;
        }

        // Track node after autowire block ends (for re-expansion insert point)
        if (need_autowire_after && !info.autowire_after) {
            info.autowire_after = member;
            need_autowire_after = false;
        }
        if (need_autoreg_after && !info.autoreg_after) {
            info.autoreg_after = member;
            need_autoreg_after = false;
        }

        // Check for AUTOINST marker (uses hasMarker for content, not just trivia)
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

        // Check for AUTOWIRE marker in trivia
        // Use hasMarkerInTrivia for more precise detection - we want to find the node
        // whose leading trivia contains /*AUTOWIRE*/
        if (hasMarkerInTrivia(*member, "/*AUTOWIRE*/")) {
            info.autowire_marker = member;
            // Note: We no longer track autowire_next since we insert BEFORE the marker
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

        // Check for end marker BEFORE adding to block
        // (so we don't add the end marker node to the block)
        bool is_end_marker = hasMarker(*member, "// End of automatics");
        if (is_end_marker) {
            if (in_autowire_block) {
                in_autowire_block = false;
                info.autowire_block_end = member;
                need_autowire_after = true;
            }
            if (in_autoreg_block) {
                in_autoreg_block = false;
                info.autoreg_block_end = member;
                need_autoreg_after = true;
            }
        }

        // Add to block only if:
        // - Still in the block (not past end marker)
        // - Not an AUTOINST node (handled separately, avoid conflict)
        // - Not the end marker node itself
        //
        // IMPORTANT: We exclude AUTOINST nodes to avoid a slang rewriter conflict.
        // Nodes in autowire_block get remove()'d, but AUTOINST nodes get replace()'d.
        // A node cannot have both operations applied - slang throws:
        //   "Node only permit one remove/replace operation"
        // By excluding AUTOINST nodes here, they're only subject to replace().
        bool is_autoinst = hasMarker(*member, "/*AUTOINST*/");
        if (in_autowire_block && !is_autoinst && !is_end_marker) {
            info.autowire_block.push_back(member);
        }
        if (in_autoreg_block && !is_autoinst && !is_end_marker) {
            info.autoreg_block.push_back(member);
        }

        // Collect user declarations (excluding auto blocks)
        if (!in_autowire_block && !in_autoreg_block) {
            if (auto name = extractDeclarationName(*member)) {
                info.existing_decls.insert(*name);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // AUTOPORTS detection - check module header's port list
    // ═══════════════════════════════════════════════════════════════════════
    if (module.header->ports &&
        module.header->ports->kind == SyntaxKind::AnsiPortList) {

        auto& ansi = module.header->ports->as<AnsiPortListSyntax>();
        info.ansi_ports = &ansi;

        // Check closeParen trivia first (most common case: /*AUTOPORTS*/ at end)
        if (hasMarkerInTokenTrivia(ansi.closeParen, "/*AUTOPORTS*/")) {
            info.has_autoports = true;
        }

        // Scan ports to find marker and separate user vs auto-generated
        bool found_marker = false;
        for (auto* port : ansi.ports) {
            // Check if this port has the AUTOPORTS marker in its trivia
            if (auto tok = port->getFirstToken()) {
                if (hasMarkerInTokenTrivia(tok, "/*AUTOPORTS*/")) {
                    info.has_autoports = true;
                    found_marker = true;
                    // This port and all after are auto-generated
                }
            }

            if (found_marker) {
                // Port after marker = auto-generated (track for removal on re-expand)
                info.autogenerated_ports.push_back(port);
            } else {
                // Port before marker = user-declared
                if (port->kind == SyntaxKind::ImplicitAnsiPort) {
                    auto& implicit = port->as<ImplicitAnsiPortSyntax>();
                    std::string name = std::string(implicit.declarator->name.valueText());
                    info.existing_ports.insert(name);
                    // Also add to existing_decls so AUTOWIRE won't duplicate
                    info.existing_decls.insert(name);
                }
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

        // Empty port name indicates a parsing failure (e.g., undefined macros in port declaration)
        if (info.name.empty()) {
            if (options_.diagnostics) {
                options_.diagnostics->addError(
                    "Port with empty name in module '" + module_name +
                    "' (likely caused by undefined macros in port declaration). "
                    "Ensure all required macros are defined via +define+ or include files.",
                    "", 0, "port_parse");
            }
            return ports;  // Return empty - caller will handle the error
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

    // Detect indent from the original instance
    std::string indent = detectIndent(*inst.node);

    // Generate the complete instance text with expanded port list
    std::string full_inst = generateFullInstanceText(inst, ports, indent);
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
            // TRIVIA HANDLING: We use preserveTrivia=true to keep AUTO_TEMPLATE comments
            // that precede the instance. These comments are "leading trivia" on the
            // instance node in slang's model.
            //
            // Side effect: This also preserves /*AUTOWIRE*/ if it was trivia on this node,
            // resulting in duplicates (we also generate it in generateAutowireText()).
            // Tool.cpp post-processing removes the duplicates.
            //
            // Trade-off: We accept duplicate cleanup rather than lose template comments.
            // A cleaner solution would involve selective trivia preservation, but slang's
            // API doesn't provide easy access to filter trivia items.
            replace(*inst.node, *mod_member, /* preserveTrivia */ true);
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
    // Note: generateAutowireText now uses getInternalNets() which only returns
    // signals that are both driven AND consumed by instances (instance-to-instance).
    // External signals are excluded by design - they should be ports, not wires.
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

    // Extract all declarations from the parsed module, including the dummy end marker
    // The dummy marker carries "// End of automatics" as its leading trivia
    // Tool.cpp will remove the localparam line but preserve the comment
    std::vector<MemberSyntax*> decl_members;
    for (auto* mod_member : mod_decl->members) {
        decl_members.push_back(const_cast<MemberSyntax*>(mod_member));
    }

    if (decl_members.empty()) {
        return;
    }

    // Find where to insert
    // For both fresh expansion and re-expansion, use the autowire_marker
    // (the first node that has /*AUTOWIRE*/ or // Beginning of automatic wires in trivia)
    // The insertBefore works even if the target node is being removed
    const MemberSyntax* insert_point = info.autowire_marker;

    if (insert_point) {
        // Insert all declarations before the insertion point
        for (auto* decl : decl_members) {
            insertBefore(*insert_point, *decl);
        }
    }
    // Note: If no insert_point found (marker is last node in module), we can't easily insert.
    // This is an edge case that would need special handling.
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

bool AutosRewriter::hasMarkerInTrivia(const SyntaxNode& node, std::string_view marker) const {
    // Check only the leading trivia of the first token, not the node content
    if (auto tok = node.getFirstToken()) {
        return hasMarkerInTokenTrivia(tok, marker);
    }
    return false;
}

bool AutosRewriter::hasMarkerInTokenTrivia(Token tok, std::string_view marker) const {
    for (const auto& trivia : tok.trivia()) {
        auto raw_text = trivia.getRawText();
        if (raw_text.find(marker) != std::string_view::npos) {
            return true;
        }
    }
    return false;
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
    const std::vector<PortInfo>& ports,
    const std::string& indent) {

    std::ostringstream oss;

    // Create template matcher for @ substitution
    TemplateMatcher matcher(inst.templ, nullptr);
    matcher.setInstance(inst.instance_name);

    // Module type and instance name (with original indent)
    oss << indent << inst.module_type << " " << inst.instance_name << " (/*AUTOINST*/\n";

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

    // Port connections get one additional indent level
    // Use detected indent as the unit (e.g., if instance has 2-space indent, ports get 4)
    std::string port_indent = indent + indent;

    // Write port connections
    for (size_t i = 0; i < all_ports.size(); ++i) {
        const auto& entry = all_ports[i];

        // Add group comment if present
        if (!entry.group_comment.empty()) {
            oss << port_indent << "// " << entry.group_comment << "\n";
        }

        // Format the port connection
        if (entry.is_unconnected) {
            oss << port_indent << "." << entry.port->name << "()";
        } else {
            oss << port_indent << "." << entry.port->name << "(" << entry.signal << ")";
        }

        if (i < all_ports.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }

    oss << indent << ");";

    return oss.str();
}

std::string AutosRewriter::generateAutowireText(
    const std::set<std::string>& existing_decls) {

    // Get internal nets only (both driven AND consumed by instances)
    // These are instance-to-instance connections that need wire declarations.
    // External signals should be ports, not wires.
    auto driven_nets = aggregator_.getInternalNets();

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

    // ════════════════════════════════════════════════════════════════════════════
    // TRIVIA MODEL WORKAROUNDS
    //
    // In slang's leading-trivia model, /*AUTOWIRE*/ is trivia on the NEXT node,
    // not a standalone entity. When we insertBefore() a node, our new content
    // appears before that node's trivia, so /*AUTOWIRE*/ would end up AFTER our
    // declarations - wrong!
    //
    // Solution: We explicitly include /*AUTOWIRE*/ in generated text. This may
    // create duplicates (original preserved via AUTOINST's preserveTrivia), but
    // Tool.cpp cleans those up. The result is correct marker placement.
    // ════════════════════════════════════════════════════════════════════════════
    oss << "\n" << indent << "/*AUTOWIRE*/\n";
    oss << indent << "// Beginning of automatic wires\n";

    for (const auto& net : to_declare) {
        oss << indent << type_str;

        std::string range = net.getRangeStr();
        if (!range.empty()) {
            oss << " " << range;
        }

        oss << " " << net.name << ";\n";
    }

    // ════════════════════════════════════════════════════════════════════════════
    // DUMMY MARKER TRICK
    //
    // Comments in slang must be trivia on a syntax node - they can't exist alone.
    // To ensure "// End of automatics" survives the rewriter and appears on its
    // own line, we create a dummy localparam that carries the comment as its
    // leading trivia. Tool.cpp removes the dummy localparam after transformation,
    // leaving just the comment.
    //
    // Alternative approaches considered:
    // - Attach comment to next real node: complex, node may not exist or may move
    // - Use a different syntax construct: localparam is harmless if cleanup fails
    // ════════════════════════════════════════════════════════════════════════════
    oss << indent << "// End of automatics\n";
    oss << indent << "localparam _SLANG_AUTOS_END_MARKER_ = 0;\n";

    return oss.str();
}

void AutosRewriter::queueAutoportsExpansion(
    [[maybe_unused]] const ModuleDeclarationSyntax& module,
    const CollectedInfo& info) {

    if (!info.has_autoports || !info.ansi_ports) {
        return;
    }

    // Remove old auto-generated ports (for re-expansion idempotency)
    for (auto* port : info.autogenerated_ports) {
        remove(*port);
    }

    // Get external input/output nets from aggregator
    auto inputs = aggregator_.getExternalInputNets();
    auto outputs = aggregator_.getExternalOutputNets();
    auto inouts = aggregator_.getInoutNets();

    // Filter out already-declared ports
    auto filterExisting = [&info](std::vector<NetInfo>& nets) {
        nets.erase(std::remove_if(nets.begin(), nets.end(),
            [&](const NetInfo& net) {
                return info.existing_ports.find(net.name) != info.existing_ports.end();
            }), nets.end());
    };

    filterExisting(inputs);
    filterExisting(outputs);
    filterExisting(inouts);

    // Check if we have anything to generate
    if (inputs.empty() && outputs.empty() && inouts.empty()) {
        return;
    }

    // Generate port declaration text
    std::ostringstream oss;
    std::string type_str = options_.use_logic ? "logic" : "wire";

    // Collect all ports with their directions
    struct PortEntry {
        const NetInfo* net;
        std::string direction;
    };
    std::vector<PortEntry> all_ports;

    for (const auto& net : outputs) {
        all_ports.push_back({&net, "output"});
    }
    for (const auto& net : inouts) {
        all_ports.push_back({&net, "inout"});
    }
    for (const auto& net : inputs) {
        all_ports.push_back({&net, "input"});
    }

    // Build port list for wrapper module
    // Include AUTOPORTS marker before the first port so it stays in the right position
    // (becomes leading trivia on the first generated port)
    for (size_t i = 0; i < all_ports.size(); ++i) {
        const auto& entry = all_ports[i];

        if (i == 0) {
            // First port gets the AUTOPORTS marker as leading trivia
            oss << "\n    /*AUTOPORTS*/\n    ";
        } else {
            oss << "\n    ";
        }

        oss << entry.direction << " " << type_str;

        std::string range = entry.net->getRangeStr();
        if (!range.empty()) {
            oss << " " << range;
        }

        oss << " " << entry.net->name;

        if (i < all_ports.size() - 1) {
            oss << ",";
        }
    }

    std::string port_text = oss.str();

    // Wrap in a module to parse as ANSI port list
    std::string wrapper = "module _wrapper_ (" + port_text + "\n);\nendmodule\n";
    auto& parsed = parse(wrapper);

    // Find the parsed module
    const ModuleDeclarationSyntax* parsed_mod = nullptr;

    if (parsed.kind == SyntaxKind::ModuleDeclaration) {
        parsed_mod = &parsed.as<ModuleDeclarationSyntax>();
    } else if (parsed.kind == SyntaxKind::CompilationUnit) {
        auto& comp_unit = parsed.as<CompilationUnitSyntax>();
        for (auto* member : comp_unit.members) {
            if (member->kind == SyntaxKind::ModuleDeclaration) {
                parsed_mod = &member->as<ModuleDeclarationSyntax>();
                break;
            }
        }
    }

    if (!parsed_mod || !parsed_mod->header->ports) {
        return;
    }

    if (parsed_mod->header->ports->kind != SyntaxKind::AnsiPortList) {
        return;
    }

    auto& parsed_ports = parsed_mod->header->ports->as<AnsiPortListSyntax>();

    // Insert each port into the original port list
    for (auto* port : parsed_ports.ports) {
        // Use makeComma() to add proper comma separator before each inserted port
        insertAtBack(info.ansi_ports->ports,
                     *const_cast<MemberSyntax*>(port),
                     makeComma());
    }
}

std::string AutosRewriter::detectIndent(const SyntaxNode& node) const {
    std::string indent = options_.indent;  // Default

    if (auto tok = node.getFirstToken()) {
        bool saw_newline = false;
        for (const auto& trivia : tok.trivia()) {
            if (trivia.kind == TriviaKind::EndOfLine) {
                saw_newline = true;
            } else if (trivia.kind == TriviaKind::Whitespace && saw_newline) {
                // Whitespace immediately after a newline is the indent
                indent = std::string(trivia.getRawText());
                saw_newline = false;  // Reset for next potential newline
            } else {
                saw_newline = false;  // Non-whitespace resets
            }
        }
    }

    return indent;
}

} // namespace slang_autos
