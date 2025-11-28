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

    oss << indent << "// End of automatic wires\n";

    return oss.str();
}

} // namespace slang_autos
