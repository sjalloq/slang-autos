#include "slang-autos/Expander.h"

#include <algorithm>
#include <regex>
#include <sstream>

namespace slang_autos {

// ============================================================================
// AutoInstExpander Implementation
// ============================================================================

AutoInstExpander::AutoInstExpander(const AutoTemplate* tmpl, DiagnosticCollector* diagnostics)
    : template_(tmpl)
    , diagnostics_(diagnostics) {
}

std::string AutoInstExpander::expand(
    const std::string& instance_name,
    const std::vector<PortInfo>& ports,
    const std::set<std::string>& manual_ports,
    const std::string& filter_pattern,
    const std::string& indent,
    bool alignment) {

    alignment_ = alignment;
    indent_ = indent;

    buildConnections(instance_name, ports, manual_ports, filter_pattern);

    if (connections_.empty()) {
        return "";
    }

    // Calculate max port name length for alignment
    size_t max_port_len = 0;
    if (alignment_) {
        for (const auto& conn : connections_) {
            max_port_len = std::max(max_port_len, conn.port_name.length());
        }
    }

    // Group connections by direction
    std::vector<PortConnection> outputs, inouts, inputs;
    for (const auto& conn : connections_) {
        if (conn.direction == "output") {
            outputs.push_back(conn);
        } else if (conn.direction == "inout") {
            inouts.push_back(conn);
        } else {
            inputs.push_back(conn);
        }
    }

    std::ostringstream oss;
    bool need_separator = false;

    // Outputs first
    if (!outputs.empty()) {
        oss << "\n" << indent_ << "// Outputs\n";
        for (size_t i = 0; i < outputs.size(); ++i) {
            bool is_last = (i == outputs.size() - 1) && inouts.empty() && inputs.empty();
            oss << indent_ << formatConnection(outputs[i], max_port_len, is_last);
        }
        need_separator = true;
    }

    // Inouts second
    if (!inouts.empty()) {
        if (need_separator) {
            oss << indent_ << "// Inouts\n";
        } else {
            oss << "\n" << indent_ << "// Inouts\n";
        }
        for (size_t i = 0; i < inouts.size(); ++i) {
            bool is_last = (i == inouts.size() - 1) && inputs.empty();
            oss << indent_ << formatConnection(inouts[i], max_port_len, is_last);
        }
        need_separator = true;
    }

    // Inputs last
    if (!inputs.empty()) {
        if (need_separator) {
            oss << indent_ << "// Inputs\n";
        } else {
            oss << "\n" << indent_ << "// Inputs\n";
        }
        for (size_t i = 0; i < inputs.size(); ++i) {
            bool is_last = (i == inputs.size() - 1);
            oss << indent_ << formatConnection(inputs[i], max_port_len, is_last);
        }
    }

    return oss.str();
}

void AutoInstExpander::buildConnections(
    const std::string& instance_name,
    const std::vector<PortInfo>& ports,
    const std::set<std::string>& manual_ports,
    const std::string& filter_pattern) {

    connections_.clear();

    // Compile filter pattern if provided
    std::optional<std::regex> filter_re;
    if (!filter_pattern.empty()) {
        try {
            filter_re = std::regex(filter_pattern);
        } catch (const std::regex_error&) {
            // Invalid pattern - treat as no filter
        }
    }

    // Create template matcher
    TemplateMatcher matcher(template_, diagnostics_);
    matcher.setInstance(instance_name);

    for (const auto& port : ports) {
        // Skip if manually connected
        if (manual_ports.find(port.name) != manual_ports.end()) {
            continue;
        }

        // Apply filter if provided
        if (filter_re) {
            if (!std::regex_search(port.name, *filter_re)) {
                continue;
            }
        }

        // Match port against template
        auto result = matcher.matchPort(port);

        PortConnection conn;
        conn.port_name = port.name;
        conn.direction = port.direction;

        if (TemplateMatcher::isSpecialValue(result.signal_name)) {
            if (result.signal_name == "_") {
                conn.is_unconnected = true;
                conn.signal_expr = "";
            } else {
                conn.is_constant = true;
                conn.signal_expr = TemplateMatcher::formatSpecialValue(result.signal_name);
            }
        } else {
            conn.signal_expr = result.signal_name;
        }

        connections_.push_back(conn);
    }
}

std::string AutoInstExpander::formatConnection(
    const PortConnection& conn,
    size_t max_port_len,
    bool is_last) const {

    std::ostringstream oss;

    oss << ".";
    oss << conn.port_name;

    // Padding for alignment
    if (alignment_ && max_port_len > 0) {
        size_t padding = max_port_len - conn.port_name.length();
        oss << std::string(padding, ' ');
    }

    oss << " (";

    if (conn.is_unconnected) {
        // Leave unconnected - empty parentheses
    } else {
        oss << conn.signal_expr;
    }

    oss << ")";

    if (!is_last) {
        oss << ",";
    }

    oss << "\n";

    return oss.str();
}

std::vector<ExpandedSignal> AutoInstExpander::getExpandedSignals(
    const std::string& instance_name,
    const std::vector<PortInfo>& ports) {

    // Build connections with no manual ports or filter
    buildConnections(instance_name, ports, {}, "");

    std::vector<ExpandedSignal> signals;

    for (const auto& conn : connections_) {
        // Skip unconnected and constant ports
        if (conn.is_unconnected || conn.is_constant) {
            continue;
        }

        // Find the port info for range information
        auto port_it = std::find_if(ports.begin(), ports.end(),
            [&](const PortInfo& p) { return p.name == conn.port_name; });

        if (port_it != ports.end()) {
            signals.emplace_back(
                conn.signal_expr,
                conn.direction,
                port_it->range_str,
                port_it->original_range_str);
        }
    }

    return signals;
}

// ============================================================================
// AutoWireExpander Implementation
// ============================================================================

AutoWireExpander::AutoWireExpander(DiagnosticCollector* diagnostics)
    : diagnostics_(diagnostics) {
}

std::string AutoWireExpander::expand(
    const std::vector<ExpandedSignal>& signals,
    const std::set<std::string>& existing_decls,
    const std::string& indent) {

    // Filter to only output signals that need declarations
    std::vector<const ExpandedSignal*> to_declare;

    for (const auto& sig : signals) {
        // Only declare outputs (they're driven by instances)
        if (sig.direction != "output") {
            continue;
        }

        // Skip if already declared
        if (existing_decls.find(sig.signal_name) != existing_decls.end()) {
            continue;
        }

        // Check if we already have this signal in our list
        bool already_in_list = std::any_of(to_declare.begin(), to_declare.end(),
            [&](const ExpandedSignal* s) { return s->signal_name == sig.signal_name; });

        if (!already_in_list) {
            to_declare.push_back(&sig);
        }
    }

    if (to_declare.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "\n" << indent << "// Beginning of automatic wires\n";

    for (const auto* sig : to_declare) {
        oss << indent << "wire ";

        // Use original range if available, otherwise resolved
        std::string range = sig->original_range_str.empty()
            ? sig->range_str
            : sig->original_range_str;

        if (!range.empty()) {
            oss << range << " ";
        }

        oss << sig->signal_name << ";\n";
    }

    oss << indent << "// End of automatics\n";

    return oss.str();
}

std::string AutoWireExpander::expandFromAggregator(
    const SignalAggregator& aggregator,
    const std::set<std::string>& existing_decls,
    const std::string& type_str,
    const std::string& indent,
    PortGrouping grouping) {

    // Get internal nets only (both driven AND consumed by instances)
    // These are instance-to-instance connections that need wire declarations.
    // External signals (driven but not consumed, or consumed but not driven)
    // should be ports, not wires.
    std::vector<NetInfo> driven_nets = aggregator.getInternalNets();

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

    // Sort according to grouping preference
    if (grouping == PortGrouping::Alphabetical) {
        std::sort(to_declare.begin(), to_declare.end(),
            [](const NetInfo& a, const NetInfo& b) { return a.name < b.name; });
    }
    // For ByDirection, we keep the original order (which comes from instance traversal)
    // In AUTOWIRE context, there's no direction grouping since these are all wires

    std::ostringstream oss;
    oss << "\n" << indent << "// Beginning of automatic wires\n";

    for (const auto& net : to_declare) {
        oss << indent << type_str;

        std::string range = net.getRangeStr();
        if (!range.empty()) {
            oss << " " << range;
        }

        oss << " " << net.name << ";\n";
    }

    oss << indent << "// End of automatics\n";

    return oss.str();
}

// ============================================================================
// SignalAggregator Implementation
// ============================================================================

void SignalAggregator::addFromInstance(
    const std::string& inst_name,
    const std::vector<PortConnection>& connections,
    const std::vector<PortInfo>& ports) {

    for (const auto& conn : connections) {
        // Skip unconnected and constant ports
        if (conn.is_unconnected || conn.is_constant) {
            continue;
        }

        // Find the port info to get width
        auto port_it = std::find_if(ports.begin(), ports.end(),
            [&](const PortInfo& p) { return p.name == conn.port_name; });

        if (port_it == ports.end()) {
            continue;
        }

        const std::string& net_name = conn.signal_expr;
        int port_width = port_it->width;

        // Get or create net usage entry
        auto& usage = nets_[net_name];
        if (usage.info.name.empty()) {
            usage.info.name = net_name;
            usage.info.width = port_width;
            if (port_width > 1) {
                usage.info.msb = port_width - 1;
                usage.info.lsb = 0;
            }
        } else {
            // Merge - take max width
            usage.info.merge(port_width);
        }

        // Track instance source
        if (std::find(usage.source_instances.begin(), usage.source_instances.end(), inst_name)
            == usage.source_instances.end()) {
            usage.source_instances.push_back(inst_name);
        }

        // Track direction usage
        if (conn.direction == "output") {
            usage.driven_by_instance = true;
        } else if (conn.direction == "input") {
            usage.consumed_by_instance = true;
        } else if (conn.direction == "inout") {
            usage.driven_by_instance = true;
            usage.consumed_by_instance = true;
            inout_nets_.insert(net_name);
        }
    }
}

std::vector<NetInfo> SignalAggregator::getInstanceDrivenNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        if (usage.driven_by_instance) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getExternalInputNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // External input: consumed by instance input but NOT driven by any instance output
        if (usage.consumed_by_instance && !usage.driven_by_instance) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getExternalOutputNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // External output: driven by instance output but NOT consumed by any instance input
        // Also exclude inouts
        if (usage.driven_by_instance && !usage.consumed_by_instance &&
            inout_nets_.find(name) == inout_nets_.end()) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getInoutNets() const {
    std::vector<NetInfo> result;
    for (const auto& net_name : inout_nets_) {
        auto it = nets_.find(net_name);
        if (it != nets_.end()) {
            result.push_back(it->second.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getInternalNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // Internal net: driven by instance output AND consumed by instance input
        // Excludes inouts (those are external bidirectional ports)
        if (usage.driven_by_instance && usage.consumed_by_instance &&
            inout_nets_.find(name) == inout_nets_.end()) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::optional<int> SignalAggregator::getNetWidth(const std::string& name) const {
    auto it = nets_.find(name);
    if (it != nets_.end()) {
        return it->second.info.width;
    }
    return std::nullopt;
}

bool SignalAggregator::isDrivenByInstance(const std::string& name) const {
    auto it = nets_.find(name);
    if (it != nets_.end()) {
        return it->second.driven_by_instance;
    }
    return false;
}

std::set<std::string> SignalAggregator::getInstanceDrivenNetNames() const {
    std::set<std::string> result;
    for (const auto& [name, usage] : nets_) {
        if (usage.driven_by_instance) {
            result.insert(name);
        }
    }
    return result;
}

// ============================================================================
// AutoRegExpander Implementation
// ============================================================================

AutoRegExpander::AutoRegExpander(DiagnosticCollector* diagnostics)
    : diagnostics_(diagnostics) {
}

std::string AutoRegExpander::expand(
    const std::vector<NetInfo>& module_outputs,
    const SignalAggregator& aggregator,
    const std::set<std::string>& existing_decls,
    const std::string& type_str,
    const std::string& indent,
    PortGrouping grouping) {

    // Get nets driven by instances (these are NOT autoreg candidates)
    std::set<std::string> instance_driven = aggregator.getInstanceDrivenNetNames();

    // Find outputs that need reg declarations:
    // - Must be a module output
    // - Not driven by any instance
    // - Not already declared by user
    std::vector<NetInfo> to_declare;
    for (const auto& output : module_outputs) {
        // Skip if driven by an instance
        if (instance_driven.find(output.name) != instance_driven.end()) {
            continue;
        }

        // Skip if already declared
        if (existing_decls.find(output.name) != existing_decls.end()) {
            continue;
        }

        to_declare.push_back(output);
    }

    if (to_declare.empty()) {
        return "";
    }

    // Sort according to grouping preference
    if (grouping == PortGrouping::Alphabetical) {
        std::sort(to_declare.begin(), to_declare.end(),
            [](const NetInfo& a, const NetInfo& b) { return a.name < b.name; });
    }

    std::ostringstream oss;
    oss << "\n" << indent << "// Beginning of automatic regs\n";

    for (const auto& net : to_declare) {
        oss << indent << type_str;

        std::string range = net.getRangeStr();
        if (!range.empty()) {
            oss << " " << range;
        }

        oss << " " << net.name << ";\n";
    }

    oss << indent << "// End of automatic regs\n";

    return oss.str();
}

// ============================================================================
// AutoPortsExpander Implementation
// ============================================================================

AutoPortsExpander::AutoPortsExpander(DiagnosticCollector* diagnostics)
    : diagnostics_(diagnostics) {
}

std::string AutoPortsExpander::formatPort(
    const NetInfo& net,
    const std::string& direction,
    const std::string& type_str,
    bool is_last) const {

    std::ostringstream oss;
    oss << direction << " " << type_str;

    std::string range = net.getRangeStr();
    if (!range.empty()) {
        oss << " " << range;
    }

    oss << " " << net.name;

    if (!is_last) {
        oss << ",";
    }

    return oss.str();
}

std::string AutoPortsExpander::expand(
    const SignalAggregator& aggregator,
    const std::set<std::string>& existing_ports,
    const std::string& type_str,
    const std::string& indent,
    PortGrouping grouping) {

    // Collect all port types
    std::vector<NetInfo> inputs = aggregator.getExternalInputNets();
    std::vector<NetInfo> outputs = aggregator.getExternalOutputNets();
    std::vector<NetInfo> inouts = aggregator.getInoutNets();

    // Filter out existing ports
    auto filterExisting = [&existing_ports](std::vector<NetInfo>& nets) {
        nets.erase(std::remove_if(nets.begin(), nets.end(),
            [&](const NetInfo& net) {
                return existing_ports.find(net.name) != existing_ports.end();
            }), nets.end());
    };

    filterExisting(inputs);
    filterExisting(outputs);
    filterExisting(inouts);

    // Check if we have anything to generate
    if (inputs.empty() && outputs.empty() && inouts.empty()) {
        return "";
    }

    std::ostringstream oss;

    if (grouping == PortGrouping::Alphabetical) {
        // Combine all and sort alphabetically
        std::vector<std::pair<NetInfo, std::string>> all_ports;
        for (const auto& net : inputs) {
            all_ports.emplace_back(net, "input");
        }
        for (const auto& net : outputs) {
            all_ports.emplace_back(net, "output");
        }
        for (const auto& net : inouts) {
            all_ports.emplace_back(net, "inout");
        }

        std::sort(all_ports.begin(), all_ports.end(),
            [](const auto& a, const auto& b) { return a.first.name < b.first.name; });

        oss << "\n" << indent << "// Beginning of automatic ports\n";
        for (size_t i = 0; i < all_ports.size(); ++i) {
            bool is_last = (i == all_ports.size() - 1);
            oss << indent << formatPort(all_ports[i].first, all_ports[i].second, type_str, is_last) << "\n";
        }
        oss << indent << "// End of automatic ports\n";
    }
    else {
        // Group by direction (verilog-mode style)
        oss << "\n";

        size_t total = inputs.size() + outputs.size() + inouts.size();
        size_t count = 0;

        // Outputs first
        if (!outputs.empty()) {
            oss << indent << "// Outputs\n";
            for (const auto& net : outputs) {
                ++count;
                bool is_last = (count == total);
                oss << indent << formatPort(net, "output", type_str, is_last) << "\n";
            }
        }

        // Inouts second
        if (!inouts.empty()) {
            oss << indent << "// Inouts\n";
            for (const auto& net : inouts) {
                ++count;
                bool is_last = (count == total);
                oss << indent << formatPort(net, "inout", type_str, is_last) << "\n";
            }
        }

        // Inputs last
        if (!inputs.empty()) {
            oss << indent << "// Inputs\n";
            for (const auto& net : inputs) {
                ++count;
                bool is_last = (count == total);
                oss << indent << formatPort(net, "input", type_str, is_last) << "\n";
            }
        }
    }

    return oss.str();
}

std::string AutoPortsExpander::expandInputs(
    const SignalAggregator& aggregator,
    const std::set<std::string>& existing_ports,
    const std::string& type_str,
    const std::string& indent) {

    std::vector<NetInfo> inputs = aggregator.getExternalInputNets();

    // Filter out existing ports
    inputs.erase(std::remove_if(inputs.begin(), inputs.end(),
        [&](const NetInfo& net) {
            return existing_ports.find(net.name) != existing_ports.end();
        }), inputs.end());

    if (inputs.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "\n" << indent << "// Beginning of automatic inputs\n";
    for (size_t i = 0; i < inputs.size(); ++i) {
        bool is_last = (i == inputs.size() - 1);
        oss << indent << formatPort(inputs[i], "input", type_str, is_last) << "\n";
    }
    oss << indent << "// End of automatic inputs\n";

    return oss.str();
}

std::string AutoPortsExpander::expandOutputs(
    const SignalAggregator& aggregator,
    const std::set<std::string>& existing_ports,
    const std::string& type_str,
    const std::string& indent) {

    std::vector<NetInfo> outputs = aggregator.getExternalOutputNets();

    // Filter out existing ports
    outputs.erase(std::remove_if(outputs.begin(), outputs.end(),
        [&](const NetInfo& net) {
            return existing_ports.find(net.name) != existing_ports.end();
        }), outputs.end());

    if (outputs.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "\n" << indent << "// Beginning of automatic outputs\n";
    for (size_t i = 0; i < outputs.size(); ++i) {
        bool is_last = (i == outputs.size() - 1);
        oss << indent << formatPort(outputs[i], "output", type_str, is_last) << "\n";
    }
    oss << indent << "// End of automatic outputs\n";

    return oss.str();
}

std::string AutoPortsExpander::expandInouts(
    const SignalAggregator& aggregator,
    const std::set<std::string>& existing_ports,
    const std::string& type_str,
    const std::string& indent) {

    std::vector<NetInfo> inouts = aggregator.getInoutNets();

    // Filter out existing ports
    inouts.erase(std::remove_if(inouts.begin(), inouts.end(),
        [&](const NetInfo& net) {
            return existing_ports.find(net.name) != existing_ports.end();
        }), inouts.end());

    if (inouts.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "\n" << indent << "// Beginning of automatic inouts\n";
    for (size_t i = 0; i < inouts.size(); ++i) {
        bool is_last = (i == inouts.size() - 1);
        oss << indent << formatPort(inouts[i], "inout", type_str, is_last) << "\n";
    }
    oss << indent << "// End of automatic inouts\n";

    return oss.str();
}

} // namespace slang_autos
