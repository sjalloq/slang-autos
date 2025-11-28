#pragma once

#include <set>
#include <string>
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

/// Signal information for AUTOWIRE generation.
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

    /// Generate wire declarations from expanded signals.
    /// @param signals Signals collected from AUTOINST expansions
    /// @param existing_decls Signal names already declared (to skip)
    /// @param indent Indentation for generated declarations
    /// @return Formatted wire declaration block
    [[nodiscard]] std::string expand(
        const std::vector<ExpandedSignal>& signals,
        const std::set<std::string>& existing_decls,
        const std::string& indent = "    ");

private:
    DiagnosticCollector* diagnostics_;
};

} // namespace slang_autos
