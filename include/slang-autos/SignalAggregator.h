#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "Diagnostics.h"
#include "TemplateMatcher.h"

namespace slang::ast {
class Scope;
}

namespace slang::syntax {
class SyntaxNode;
}

namespace slang_autos {

/// Check if a string is a Verilog constant (sized/unsized literal).
/// Examples: 1'b0, 8'hFF, 32'd100, '0, '1, 'z, 'x
[[nodiscard]] bool isVerilogConstant(const std::string& s);

/// Extract signal identifiers from an expression string.
/// Parses the string and extracts identifiers. Use extractIdentifiersFromSyntax
/// when you already have an AST node to avoid re-parsing.
/// Example: "{1'b0, mac_phy_rate}" -> ["mac_phy_rate"]
/// Example: "foo[7:0]" -> ["foo"]
[[nodiscard]] std::vector<std::string> extractIdentifiers(const std::string& expr);

/// Extract the maximum bit index from an expression string.
/// Used to determine signal width when templates produce bit selects.
/// Example: "signal[7]" -> 7
/// Example: "signal[63:32]" -> 63
/// Example: "signal" -> -1 (no bit select)
[[nodiscard]] int extractMaxBitIndex(const std::string& expr);

/// Check if an expression is a concatenation (top-level {...}).
/// Example: "{sig_a, sig_b}" -> true
/// Example: "signal" -> false
/// Example: "{1'b0, sig_a}" -> true
[[nodiscard]] bool isConcatenation(const std::string& expr);

/// Extract signal identifiers directly from a syntax node.
/// More efficient than extractIdentifiers(string) when AST is already available.
/// Traverses the syntax tree and collects all identifier names.
[[nodiscard]] std::vector<std::string> extractIdentifiersFromSyntax(
    const slang::syntax::SyntaxNode& node);

/// A single port connection in the expansion output.
struct PortConnection {
    std::string port_name;      ///< Name of the port
    std::string signal_expr;    ///< Signal expression for output generation
    std::string direction;      ///< "input", "output", "inout"
    std::vector<std::string> signal_identifiers; ///< Extracted signal names (pre-computed)
    bool is_unconnected = false;///< Port left unconnected (via _ template)
    bool is_constant = false;   ///< Connected to constant ('0, '1, 'z)
    bool is_concatenation = false; ///< Expression is a concatenation {a, b}

    PortConnection() = default;
    PortConnection(std::string port, std::string signal, std::string dir)
        : port_name(std::move(port))
        , signal_expr(std::move(signal))
        , direction(std::move(dir)) {}
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
    int width = 1;                  ///< Maximum width across all connections (of element type for arrays)
    std::optional<int> msb;         ///< MSB of widest connection
    std::optional<int> lsb;         ///< LSB (usually 0)
    std::string type_str = "logic"; ///< Declaration type
    std::string original_range_str; ///< Original syntax: "[WIDTH-1:0]" or "[7:0][3:0]"
    std::string range_str;          ///< Resolved packed range preserving structure: "[3:0][7:0]"
    std::string array_dims;         ///< Unpacked array dimensions: " [3:0][1:0]" (after name)
    bool is_signed = false;

    NetInfo() = default;
    explicit NetInfo(std::string n, int w = 1, std::string orig_range = "", std::string resolved_range = "", std::string arr_dims = "")
        : name(std::move(n)), width(w), original_range_str(std::move(orig_range)), range_str(std::move(resolved_range)), array_dims(std::move(arr_dims)) {
        if (w > 1) {
            msb = w - 1;
            lsb = 0;
        }
    }

    /// Merge with another connection (take max width, keep ranges from widest)
    void merge(int other_width, const std::string& other_original_range = "", const std::string& other_resolved_range = "", const std::string& other_array_dims = "") {
        if (other_width > width) {
            width = other_width;
            msb = other_width - 1;
            lsb = 0;
            // Use the ranges from the widest connection
            if (!other_original_range.empty()) {
                original_range_str = other_original_range;
            }
            if (!other_resolved_range.empty()) {
                range_str = other_resolved_range;
            }
            if (!other_array_dims.empty()) {
                array_dims = other_array_dims;
            }
        }
    }

    /// Get the packed range string for declarations
    /// @param prefer_original If true, prefers original syntax (e.g., [WIDTH-1:0])
    ///                        If false, returns resolved values preserving structure (e.g., [3:0][7:0])
    [[nodiscard]] std::string getRangeStr(bool prefer_original = true) const {
        if (prefer_original && !original_range_str.empty()) return original_range_str;
        if (!range_str.empty()) return range_str;
        if (width <= 1) return "";
        return "[" + std::to_string(width - 1) + ":0]";
    }

    /// Get the unpacked array dimensions (go after the signal name)
    [[nodiscard]] std::string getArrayDims() const {
        return array_dims;
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

    /// Get nets used as instance inputs but NOT driven by any instance output - for AUTOPORTS inputs
    [[nodiscard]] std::vector<NetInfo> getExternalInputNets() const;

    /// Get nets driven by instances but NOT consumed by any instance input - for AUTOPORTS outputs
    [[nodiscard]] std::vector<NetInfo> getExternalOutputNets() const;

    /// Get inout nets - for AUTOPORTS inouts
    [[nodiscard]] std::vector<NetInfo> getInoutNets() const;

    /// Get internal nets (driven AND consumed by instances) - for AUTOLOGIC
    /// These are instance-to-instance connections that need logic declarations.
    [[nodiscard]] std::vector<NetInfo> getInternalNets() const;

    /// Look up aggregated net info by name. Returns nullptr if not found.
    [[nodiscard]] const NetInfo* getNetInfo(const std::string& name) const;

    /// Register an unused signal (for capturing discarded output bits).
    /// @param name Signal name (e.g., "unused_data_u_inst")
    /// @param width Width of the unused portion
    void addUnusedSignal(const std::string& name, int width);

    /// Get all registered unused signals - for AUTOLOGIC declarations.
    [[nodiscard]] const std::vector<NetInfo>& getUnusedSignals() const;

private:
    std::unordered_map<std::string, NetUsage> nets_;
    std::set<std::string> inout_nets_;  ///< Nets connected to inout ports
    std::set<std::string> concatenation_nets_;  ///< Nets from concatenation expressions (internal-only)
    std::vector<NetInfo> unused_signals_;  ///< Unused signals for output width adaptation
};

} // namespace slang_autos
