#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Diagnostics.h"
#include "Parser.h"

namespace slang_autos {

/// Port information extracted from module definitions.
/// Contains both resolved values (for connection) and original syntax (for declarations).
struct PortInfo {
    std::string name;               ///< Port name
    std::string direction;          ///< "input", "output", "inout"
    std::string type_str = "logic"; ///< Type: "wire", "logic", "reg", etc.
    int width = 1;                  ///< Resolved bit width
    std::string range_str;          ///< Resolved range: "[7:0]"
    std::string original_range_str; ///< Original syntax: "[WIDTH-1:0]"
    std::optional<int> msb;         ///< Most significant bit (if range)
    std::optional<int> lsb;         ///< Least significant bit (if range)
    bool is_signed = false;
    bool is_array = false;
    std::string array_dims;         ///< Array dimensions if any

    PortInfo() = default;
    PortInfo(std::string n, std::string dir, int w = 1)
        : name(std::move(n)), direction(std::move(dir)), width(w) {}

    /// Get the range string, optionally preferring original syntax
    [[nodiscard]] std::string getRangeStr(bool prefer_original = true) const {
        if (prefer_original && !original_range_str.empty()) {
            return original_range_str;
        }
        return range_str;
    }
};

/// Result of matching a port against template rules.
struct MatchResult {
    std::string signal_name;            ///< Computed signal name after substitution
    const TemplateRule* matched_rule;   ///< Rule that matched (nullptr for default)

    MatchResult() : matched_rule(nullptr) {}
    MatchResult(std::string name, const TemplateRule* rule = nullptr)
        : signal_name(std::move(name)), matched_rule(rule) {}
};

/// Matches ports against template rules and performs variable substitution.
/// Supports:
/// - Port captures: $1, $2, ${1}, ${2} from port pattern regex groups
/// - Instance captures: %1, %2, %{1}, %{2} from instance pattern regex groups
/// - Built-in variables: port.name, port.width, port.range, inst.name
class TemplateMatcher {
public:
    /// Construct a matcher with an optional template
    explicit TemplateMatcher(
        const AutoTemplate* tmpl = nullptr,
        DiagnosticCollector* diagnostics = nullptr);

    /// Set the current instance and extract captures from instance pattern.
    /// @param instance_name The instance name to match
    /// @return true if instance matches the template pattern (or no template)
    bool setInstance(const std::string& instance_name);

    /// Match a port against template rules and compute signal name.
    /// @param port Port information
    /// @return Match result with signal name and matched rule
    [[nodiscard]] MatchResult matchPort(const PortInfo& port);

    /// Check if a signal name is a special value (_, '0, '1, 'z)
    [[nodiscard]] static bool isSpecialValue(const std::string& signal);

    /// Format a special value for Verilog output.
    /// _ -> empty (unconnected), '0 -> 1'b0, etc.
    [[nodiscard]] static std::string formatSpecialValue(const std::string& signal);

    /// Get the current instance name
    [[nodiscard]] const std::string& instanceName() const { return inst_name_; }

private:
    /// Apply variable substitution to a signal expression.
    /// @param expr Signal expression with placeholders
    /// @param port Port information for built-in variables
    /// @param port_captures Captured groups from port pattern
    /// @return Substituted signal name
    std::string substitute(
        const std::string& expr,
        const PortInfo& port,
        const std::vector<std::string>& port_captures);

    /// Evaluate ternary expressions in a signal expression.
    /// Supports: condition ? true_value : false_value
    /// where condition is "0" or "1" (from port.input etc. substitution)
    /// @param expr Expression that may contain a ternary
    /// @return Evaluated result or original expression if not a ternary
    std::string evaluateTernary(const std::string& expr);

    const AutoTemplate* template_;
    DiagnosticCollector* diagnostics_;
    std::string inst_name_;
    std::vector<std::string> inst_captures_;
    std::set<std::string> warned_unresolved_;  // Avoid duplicate warnings
};

} // namespace slang_autos
