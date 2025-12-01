#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "Diagnostics.h"
#include "Parser.h"
#include "TemplateMatcher.h"

namespace slang_autos {

/// A single port connection in the expansion output.
struct PortConnection {
    std::string port_name;      ///< Name of the port
    std::string signal_expr;    ///< Signal expression to connect
    std::string direction;      ///< "input", "output", "inout"
    bool is_unconnected = false;///< Port left unconnected (via _ template)
    bool is_constant = false;   ///< Connected to constant ('0, '1, 'z)

    PortConnection() = default;
    PortConnection(std::string port, std::string signal, std::string dir)
        : port_name(std::move(port))
        , signal_expr(std::move(signal))
        , direction(std::move(dir)) {}
};

/// Signal information for AUTOWIRE generation (legacy, kept for compatibility).
struct ExpandedSignal {
    std::string signal_name;
    std::string direction;
    std::string range_str;          ///< Resolved range
    std::string original_range_str; ///< Original syntax for declarations

    ExpandedSignal() = default;
    ExpandedSignal(std::string name, std::string dir, std::string range, std::string orig_range = "")
        : signal_name(std::move(name))
        , direction(std::move(dir))
        , range_str(std::move(range))
        , original_range_str(std::move(orig_range)) {}
};

/// Grouping/sorting preference for generated declarations and ports.
enum class PortGrouping {
    ByDirection,    ///< Group by input/output/inout (verilog-mode style)
    Alphabetical    ///< Sort all alphabetically by name
};

/// Net information aggregated across all instance connections.
/// A net has no inherent direction - direction is a property of the port it connects to.
struct NetInfo {
    std::string name;               ///< Net name
    int width = 1;                  ///< Maximum width across all connections
    std::optional<int> msb;         ///< MSB of widest connection
    std::optional<int> lsb;         ///< LSB (usually 0)
    std::string type_str = "logic"; ///< Declaration type
    bool is_signed = false;

    NetInfo() = default;
    NetInfo(std::string n, int w = 1)
        : name(std::move(n)), width(w) {
        if (w > 1) {
            msb = w - 1;
            lsb = 0;
        }
    }

    /// Merge with another connection (take max width)
    void merge(int other_width) {
        if (other_width > width) {
            width = other_width;
            msb = other_width - 1;
            lsb = 0;
        }
    }

    /// Get the range string for declarations
    [[nodiscard]] std::string getRangeStr() const {
        if (width <= 1) return "";
        return "[" + std::to_string(width - 1) + ":0]";
    }
};

/// Track how a net is used across all instances.
struct NetUsage {
    NetInfo info;
    bool driven_by_instance = false;   ///< Connected to an instance output/inout
    bool consumed_by_instance = false; ///< Connected to an instance input/inout
    std::vector<std::string> source_instances; ///< Instances using this net
};

/// Aggregates nets across all AUTOINSTs, resolving width conflicts.
/// Tracks which nets are driven by instance outputs and which are consumed by instance inputs.
class SignalAggregator {
public:
    /// Add port connections from an instance.
    /// For each port: record the net name, its width, and whether it's input/output/inout.
    void addFromInstance(const std::string& inst_name,
                        const std::vector<PortConnection>& connections,
                        const std::vector<PortInfo>& ports);

    /// Get all nets driven by instances (outputs/inouts) - for AUTOWIRE
    [[nodiscard]] std::vector<NetInfo> getInstanceDrivenNets() const;

    /// Get nets used as instance inputs but NOT driven by any instance output - for AUTOINPUT
    [[nodiscard]] std::vector<NetInfo> getExternalInputNets() const;

    /// Get nets driven by instances but NOT consumed by any instance input - for AUTOOUTPUT
    [[nodiscard]] std::vector<NetInfo> getExternalOutputNets() const;

    /// Get inout nets - for AUTOINOUT
    [[nodiscard]] std::vector<NetInfo> getInoutNets() const;

    /// Get internal nets (driven AND consumed by instances) - for AUTOWIRE
    /// These are instance-to-instance connections that need wire declarations.
    [[nodiscard]] std::vector<NetInfo> getInternalNets() const;

    /// Get the resolved width for a net (for width-adjusted connections)
    [[nodiscard]] std::optional<int> getNetWidth(const std::string& name) const;

    /// Check if a net is driven by any instance
    [[nodiscard]] bool isDrivenByInstance(const std::string& name) const;

    /// Get all net names that are driven by instances
    [[nodiscard]] std::set<std::string> getInstanceDrivenNetNames() const;

private:
    std::unordered_map<std::string, NetUsage> nets_;
    std::set<std::string> inout_nets_;  ///< Nets connected to inout ports
};

/// Expands AUTOINST comments into port connection lists.
/// Handles template matching, filtering, and formatting.
class AutoInstExpander {
public:
    /// Construct expander with optional template and diagnostics
    explicit AutoInstExpander(
        const AutoTemplate* tmpl = nullptr,
        DiagnosticCollector* diagnostics = nullptr);

    /// Expand ports for an instance.
    /// @param instance_name Name of the instance
    /// @param ports All ports from the module definition
    /// @param manual_ports Ports already manually connected (to skip)
    /// @param filter_pattern Optional regex to filter ports
    /// @param indent Indentation string for each line
    /// @param alignment Align port names in output
    /// @return Formatted expansion text
    [[nodiscard]] std::string expand(
        const std::string& instance_name,
        const std::vector<PortInfo>& ports,
        const std::set<std::string>& manual_ports,
        const std::string& filter_pattern = "",
        const std::string& indent = "    ",
        bool alignment = true);

    /// Get the list of expanded connections (after expand() is called).
    [[nodiscard]] const std::vector<PortConnection>& connections() const { return connections_; }

    /// Get signals that were expanded (for AUTOWIRE use).
    /// @param instance_name Name of the instance
    /// @param ports All ports from the module definition
    /// @return List of expanded signals with their ranges
    [[nodiscard]] std::vector<ExpandedSignal> getExpandedSignals(
        const std::string& instance_name,
        const std::vector<PortInfo>& ports);

private:
    /// Format a single port connection line
    std::string formatConnection(
        const PortConnection& conn,
        size_t max_port_len,
        bool is_last) const;

    /// Build connection list (shared by expand() and getExpandedSignals())
    void buildConnections(
        const std::string& instance_name,
        const std::vector<PortInfo>& ports,
        const std::set<std::string>& manual_ports,
        const std::string& filter_pattern);

    const AutoTemplate* template_;
    DiagnosticCollector* diagnostics_;
    std::vector<PortConnection> connections_;
    bool alignment_ = true;
    std::string indent_ = "    ";
};

/// Expands AUTOWIRE comments into wire declarations.
class AutoWireExpander {
public:
    explicit AutoWireExpander(DiagnosticCollector* diagnostics = nullptr);

    /// Generate wire declarations from expanded signals (legacy interface).
    /// @param signals Signals collected from AUTOINST expansions
    /// @param existing_decls Signal names already declared (to skip)
    /// @param indent Indentation for generated declarations
    /// @return Formatted wire declaration block
    [[nodiscard]] std::string expand(
        const std::vector<ExpandedSignal>& signals,
        const std::set<std::string>& existing_decls,
        const std::string& indent = "    ");

    /// Generate wire declarations from SignalAggregator data.
    /// Uses internal nets (both driven AND consumed by instances).
    /// These are instance-to-instance connections that need wire declarations.
    /// External signals become ports, not wires.
    /// @param aggregator Signal aggregator with all instance connections
    /// @param existing_decls Signal names already declared by user (to skip)
    /// @param type_str Declaration type ("wire", "logic", etc.)
    /// @param indent Indentation for generated declarations
    /// @param grouping How to group/sort the declarations
    /// @return Formatted wire declaration block
    [[nodiscard]] std::string expandFromAggregator(
        const SignalAggregator& aggregator,
        const std::set<std::string>& existing_decls,
        const std::string& type_str = "logic",
        const std::string& indent = "    ",
        PortGrouping grouping = PortGrouping::ByDirection);

private:
    DiagnosticCollector* diagnostics_;
};

/// Expands AUTOREG comments into reg declarations.
/// Generates regs for module outputs NOT driven by instances.
class AutoRegExpander {
public:
    explicit AutoRegExpander(DiagnosticCollector* diagnostics = nullptr);

    /// Generate reg declarations for module outputs not driven by instances.
    /// Algorithm: Module outputs - Instance-driven nets - User-declared = AUTOREG
    /// @param module_outputs All output ports of the current module
    /// @param aggregator Signal aggregator with all instance connections
    /// @param existing_decls Signal names already declared by user (to skip)
    /// @param type_str Declaration type ("reg", "logic", etc.)
    /// @param indent Indentation for generated declarations
    /// @param grouping How to group/sort the declarations
    /// @return Formatted reg declaration block
    [[nodiscard]] std::string expand(
        const std::vector<NetInfo>& module_outputs,
        const SignalAggregator& aggregator,
        const std::set<std::string>& existing_decls,
        const std::string& type_str = "logic",
        const std::string& indent = "    ",
        PortGrouping grouping = PortGrouping::ByDirection);

private:
    DiagnosticCollector* diagnostics_;
};

/// Expands AUTOPORTS comments into ANSI-style port declarations.
/// Generates combined input/output/inout declarations for module port lists.
class AutoPortsExpander {
public:
    explicit AutoPortsExpander(DiagnosticCollector* diagnostics = nullptr);

    /// Generate ANSI-style port declarations for all port types.
    /// @param aggregator Signal aggregator with all instance connections
    /// @param existing_ports Port names already declared manually (to skip)
    /// @param type_str Type to use ("logic" for SV, "wire" for Verilog)
    /// @param indent Indentation for generated declarations
    /// @param grouping How to group/sort the ports
    /// @return Formatted port declaration list (without trailing comma)
    [[nodiscard]] std::string expand(
        const SignalAggregator& aggregator,
        const std::set<std::string>& existing_ports,
        const std::string& type_str = "logic",
        const std::string& indent = "    ",
        PortGrouping grouping = PortGrouping::ByDirection);

    /// Generate AUTOINPUT declarations (external inputs only).
    [[nodiscard]] std::string expandInputs(
        const SignalAggregator& aggregator,
        const std::set<std::string>& existing_ports,
        const std::string& type_str = "logic",
        const std::string& indent = "    ");

    /// Generate AUTOOUTPUT declarations (external outputs only).
    [[nodiscard]] std::string expandOutputs(
        const SignalAggregator& aggregator,
        const std::set<std::string>& existing_ports,
        const std::string& type_str = "logic",
        const std::string& indent = "    ");

    /// Generate AUTOINOUT declarations (inout ports only).
    [[nodiscard]] std::string expandInouts(
        const SignalAggregator& aggregator,
        const std::set<std::string>& existing_ports,
        const std::string& type_str = "logic",
        const std::string& indent = "    ");

private:
    /// Format a single ANSI-style port declaration
    std::string formatPort(const NetInfo& net, const std::string& direction,
                           const std::string& type_str, bool is_last) const;

    DiagnosticCollector* diagnostics_;
};

} // namespace slang_autos
