#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "Diagnostics.h"
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
    std::string original_range_str; ///< Original syntax: "[WIDTH-1:0]" or "[7:0][3:0]"
    bool is_signed = false;

    NetInfo() = default;
    explicit NetInfo(std::string n, int w = 1, std::string orig_range = "")
        : name(std::move(n)), width(w), original_range_str(std::move(orig_range)) {
        if (w > 1) {
            msb = w - 1;
            lsb = 0;
        }
    }

    /// Merge with another connection (take max width, keep original range from widest)
    void merge(int other_width, const std::string& other_original_range = "") {
        if (other_width > width) {
            width = other_width;
            msb = other_width - 1;
            lsb = 0;
            // Use the original range from the widest connection
            if (!other_original_range.empty()) {
                original_range_str = other_original_range;
            }
        }
    }

    /// Get the range string for declarations
    /// @param prefer_original If true, prefers original syntax (e.g., [WIDTH-1:0])
    ///                        If false, returns resolved values (e.g., [7:0])
    [[nodiscard]] std::string getRangeStr(bool prefer_original = true) const {
        if (prefer_original && !original_range_str.empty()) return original_range_str;
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

    /// Get nets used as instance inputs but NOT driven by any instance output - for AUTOPORTS inputs
    [[nodiscard]] std::vector<NetInfo> getExternalInputNets() const;

    /// Get nets driven by instances but NOT consumed by any instance input - for AUTOPORTS outputs
    [[nodiscard]] std::vector<NetInfo> getExternalOutputNets() const;

    /// Get inout nets - for AUTOPORTS inouts
    [[nodiscard]] std::vector<NetInfo> getInoutNets() const;

    /// Get internal nets (driven AND consumed by instances) - for AUTOLOGIC
    /// These are instance-to-instance connections that need logic declarations.
    [[nodiscard]] std::vector<NetInfo> getInternalNets() const;

private:
    std::unordered_map<std::string, NetUsage> nets_;
    std::set<std::string> inout_nets_;  ///< Nets connected to inout ports
};

} // namespace slang_autos
