#pragma once

#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "CompilationUtils.h"  // For PortInfo
#include "Diagnostics.h"
#include "Parser.h"

namespace slang_autos {

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

    /// Evaluate math functions in a signal expression.
    /// Supports: add(a,b), sub(a,b), mul(a,b), div(a,b), mod(a,b)
    /// Arguments must be integers after variable substitution.
    /// Nested functions are supported: mod(add(@, 1), 2)
    /// @param expr Expression that may contain math functions
    /// @return Expression with math functions evaluated
    std::string evaluateMathFunctions(const std::string& expr);

    /// Get or compile a regex pattern, caching for reuse.
    /// @param pattern The regex pattern string
    /// @return Pointer to cached regex, or nullptr if pattern is invalid
    const std::regex* getOrCompileRegex(const std::string& pattern);

    const AutoTemplate* template_;
    DiagnosticCollector* diagnostics_;
    std::string inst_name_;
    std::vector<std::string> inst_captures_;
    std::set<std::string> warned_unresolved_;  // Avoid duplicate warnings

    /// Cache of compiled regex patterns (keyed by pattern string)
    /// This avoids recompiling the same pattern for each port match.
    std::unordered_map<std::string, std::regex> regex_cache_;

    /// Set of patterns that failed to compile (to avoid repeated error reports)
    std::set<std::string> invalid_patterns_;
};

} // namespace slang_autos
