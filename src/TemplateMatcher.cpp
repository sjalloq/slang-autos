#include "slang-autos/TemplateMatcher.h"

#include <regex>
#include <unordered_map>

namespace slang_autos {

namespace {

// Special value mappings
const std::unordered_map<std::string, std::string> SPECIAL_VALUE_MAP = {
    {"_", ""},       // Unconnected
    {"'0", "'0"},    // Constant 0 (unsized literal)
    {"'1", "'1"},    // Constant 1 (unsized literal)
    {"'z", "'z"},    // High impedance (unsized literal)
};

} // anonymous namespace

TemplateMatcher::TemplateMatcher(const AutoTemplate* tmpl, DiagnosticCollector* diagnostics)
    : template_(tmpl)
    , diagnostics_(diagnostics) {
}

bool TemplateMatcher::setInstance(const std::string& instance_name) {
    inst_name_ = instance_name;
    inst_captures_.clear();

    if (!template_) {
        return true;
    }

    // Match instance name against template's instance pattern
    // Default pattern extracts first number from instance name (verilog-mode compatible)
    try {
        std::smatch match;
        bool use_default = template_->instance_pattern.empty();

        if (use_default) {
            // Default: search for first number anywhere in instance name
            std::regex default_pattern("([0-9]+)");
            if (std::regex_search(instance_name, match, default_pattern)) {
                inst_captures_.push_back(match[1].str());
            }
            return true;
        }

        // User-provided pattern: match entire instance name
        std::regex pattern(template_->instance_pattern);
        if (std::regex_match(instance_name, match, pattern)) {
            // Extract capture groups (skip match[0] which is full match)
            for (size_t i = 1; i < match.size(); ++i) {
                inst_captures_.push_back(match[i].str());
            }
            return true;
        } else {
            // Pattern didn't match - still use template but no captures
            inst_captures_.clear();
            return true;
        }
    } catch (const std::regex_error& e) {
        // Invalid regex - warn and treat as literal match
        if (diagnostics_) {
            diagnostics_->addWarning(
                "Invalid regex in instance pattern '" + template_->instance_pattern +
                "': " + e.what() + ". Treating as literal match.",
                "", 0, "template_regex");
        }
        return instance_name == template_->instance_pattern;
    }
}

const std::regex* TemplateMatcher::getOrCompileRegex(const std::string& pattern) {
    // Check if already cached
    auto it = regex_cache_.find(pattern);
    if (it != regex_cache_.end()) {
        return &it->second;
    }

    // Check if we already know it's invalid
    if (invalid_patterns_.find(pattern) != invalid_patterns_.end()) {
        return nullptr;
    }

    // Try to compile the regex
    try {
        auto [inserted_it, success] = regex_cache_.emplace(pattern, std::regex(pattern));
        return &inserted_it->second;
    } catch (const std::regex_error& e) {
        // Mark as invalid, warn, and return nullptr
        invalid_patterns_.insert(pattern);
        if (diagnostics_) {
            diagnostics_->addWarning(
                "Invalid regex in port pattern '" + pattern + "': " + e.what() +
                ". Pattern will be skipped.",
                "", 0, "template_regex");
        }
        return nullptr;
    }
}

MatchResult TemplateMatcher::matchPort(const PortInfo& port) {
    if (!template_) {
        // No template - default to port name
        return MatchResult(port.name);
    }

    // Try each rule in order (first match wins)
    for (const auto& rule : template_->rules) {
        // Get cached regex (or compile and cache it)
        const std::regex* pattern = getOrCompileRegex(rule.port_pattern);

        if (pattern) {
            std::smatch match;

            if (std::regex_match(port.name, match, *pattern)) {
                // Extract port captures
                std::vector<std::string> port_captures;
                for (size_t i = 1; i < match.size(); ++i) {
                    port_captures.push_back(match[i].str());
                }

                // Apply substitution, evaluate math functions, then ternary expressions
                std::string signal_name = substitute(rule.signal_expr, port, port_captures);
                signal_name = evaluateMathFunctions(signal_name);
                signal_name = evaluateTernary(signal_name);

                // Warn if assigning a constant to an output port
                if (diagnostics_ && port.direction == "output" &&
                    (signal_name == "'0" || signal_name == "'1" || signal_name == "'z")) {
                    diagnostics_->addWarning(
                        "Constant '" + signal_name + "' assigned to output port '" + port.name +
                        "'. Use ternary expression to handle direction, e.g.: port.input ? " +
                        signal_name + " : _",
                        template_ ? template_->file_path : "",
                        template_ ? template_->line_number : 0,
                        "constant_output");
                }

                return MatchResult(signal_name, &rule);
            }
        } else {
            // Invalid regex - try literal match
            if (rule.port_pattern == port.name) {
                std::string signal_name = substitute(rule.signal_expr, port, {});
                signal_name = evaluateMathFunctions(signal_name);
                signal_name = evaluateTernary(signal_name);

                // Warn if assigning a constant to an output port
                if (diagnostics_ && port.direction == "output" &&
                    (signal_name == "'0" || signal_name == "'1" || signal_name == "'z")) {
                    diagnostics_->addWarning(
                        "Constant '" + signal_name + "' assigned to output port '" + port.name +
                        "'. Use ternary expression to handle direction, e.g.: port.input ? " +
                        signal_name + " : _",
                        template_ ? template_->file_path : "",
                        template_ ? template_->line_number : 0,
                        "constant_output");
                }

                return MatchResult(signal_name, &rule);
            }
        }
    }

    // No rule matched - default to port name
    return MatchResult(port.name);
}

std::string TemplateMatcher::substitute(
    const std::string& expr,
    const PortInfo& port,
    const std::vector<std::string>& port_captures) {

    std::string result = expr;

    // Substitute port captures: $1, $2, ... or ${1}, ${2}, ...
    for (size_t i = 0; i < port_captures.size(); ++i) {
        std::string var = "$" + std::to_string(i + 1);
        std::string var_brace = "${" + std::to_string(i + 1) + "}";

        size_t pos;
        while ((pos = result.find(var_brace)) != std::string::npos) {
            result.replace(pos, var_brace.length(), port_captures[i]);
        }
        while ((pos = result.find(var)) != std::string::npos) {
            result.replace(pos, var.length(), port_captures[i]);
        }
    }

    // $0 is full port name
    {
        size_t pos;
        while ((pos = result.find("${0}")) != std::string::npos) {
            result.replace(pos, 4, port.name);
        }
        while ((pos = result.find("$0")) != std::string::npos) {
            result.replace(pos, 2, port.name);
        }
    }

    // Substitute instance captures: %1, %2, ... or %{1}, %{2}, ...
    // Also support @ as alias for %1 (verilog-mode compatibility)
    for (size_t i = 0; i < inst_captures_.size(); ++i) {
        std::string var = "%" + std::to_string(i + 1);
        std::string var_brace = "%{" + std::to_string(i + 1) + "}";

        size_t pos;
        while ((pos = result.find(var_brace)) != std::string::npos) {
            result.replace(pos, var_brace.length(), inst_captures_[i]);
        }
        while ((pos = result.find(var)) != std::string::npos) {
            result.replace(pos, var.length(), inst_captures_[i]);
        }

        // @ is alias for %1 (first instance capture)
        if (i == 0) {
            while ((pos = result.find('@')) != std::string::npos) {
                result.replace(pos, 1, inst_captures_[0]);
            }
        }
    }

    // %0 is full instance name
    {
        size_t pos;
        while ((pos = result.find("%{0}")) != std::string::npos) {
            result.replace(pos, 4, inst_name_);
        }
        while ((pos = result.find("%0")) != std::string::npos) {
            result.replace(pos, 2, inst_name_);
        }
    }

    // Substitute built-in variables
    {
        size_t pos;
        while ((pos = result.find("port.name")) != std::string::npos) {
            result.replace(pos, 9, port.name);
        }
        while ((pos = result.find("port.width")) != std::string::npos) {
            result.replace(pos, 10, std::to_string(port.width));
        }
        while ((pos = result.find("port.range")) != std::string::npos) {
            result.replace(pos, 10, port.range_str);
        }
        while ((pos = result.find("port.direction")) != std::string::npos) {
            result.replace(pos, 14, port.direction);
        }
        // Direction boolean variables (for ternary expressions)
        std::string is_input = (port.direction == "input") ? "1" : "0";
        std::string is_output = (port.direction == "output") ? "1" : "0";
        std::string is_inout = (port.direction == "inout") ? "1" : "0";

        while ((pos = result.find("port.input")) != std::string::npos) {
            result.replace(pos, 10, is_input);
        }
        while ((pos = result.find("port.output")) != std::string::npos) {
            result.replace(pos, 11, is_output);
        }
        while ((pos = result.find("port.inout")) != std::string::npos) {
            result.replace(pos, 10, is_inout);
        }
        while ((pos = result.find("inst.name")) != std::string::npos) {
            result.replace(pos, 9, inst_name_);
        }
    }

    // Check for unresolved substitution variables and warn
    // Include @ as instance capture variable
    static const std::regex unresolved_re(R"((\$\{?\d+\}?|%\{?\d+\}?|@))");
    std::sregex_iterator it(result.begin(), result.end(), unresolved_re);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        std::string var = (*it)[1].str();
        std::string warn_key = inst_name_ + ":" + port.name + ":" + var;

        if (warned_unresolved_.find(warn_key) == warned_unresolved_.end()) {
            warned_unresolved_.insert(warn_key);

            if (diagnostics_) {
                if (var[0] == '$') {
                    diagnostics_->addWarning(
                        "Unresolved port capture '" + var +
                        "' in signal expression for port '" + port.name +
                        "'. Check that your port pattern has enough capture groups.",
                        template_ ? template_->file_path : "",
                        template_ ? template_->line_number : 0,
                        "unresolved_capture");
                } else {
                    diagnostics_->addWarning(
                        "Unresolved instance capture '" + var +
                        "' for instance '" + inst_name_ +
                        "'. Check that your instance pattern has enough capture groups "
                        "(@ requires a number in the instance name).",
                        template_ ? template_->file_path : "",
                        template_ ? template_->line_number : 0,
                        "unresolved_capture");
                }
            }
        }
    }

    return result;
}

std::string TemplateMatcher::evaluateTernary(const std::string& expr) {
    // Match: condition ? true_value : false_value
    // condition is "0" or "1" (after port.input etc. substitution)
    static const std::regex ternary_re(R"(^\s*(0|1)\s*\?\s*(.+?)\s*:\s*(.+?)\s*$)");
    std::smatch match;

    if (std::regex_match(expr, match, ternary_re)) {
        bool condition = (match[1].str() == "1");
        return condition ? match[2].str() : match[3].str();
    }
    return expr;  // Not a ternary, return as-is
}

bool TemplateMatcher::isSpecialValue(const std::string& signal) {
    return SPECIAL_VALUE_MAP.find(signal) != SPECIAL_VALUE_MAP.end();
}

std::string TemplateMatcher::formatSpecialValue(const std::string& signal) {
    auto it = SPECIAL_VALUE_MAP.find(signal);
    if (it != SPECIAL_VALUE_MAP.end()) {
        return it->second;
    }
    return signal;
}

std::string TemplateMatcher::evaluateMathFunctions(const std::string& expr) {
    std::string result = expr;
    bool changed = true;

    // Iteratively evaluate innermost function calls until none remain
    while (changed) {
        changed = false;

        // Match function calls where both arguments are integers (innermost first)
        // Pattern: func(int, int) where func is add|sub|mul|div|mod
        static const std::regex func_re(
            R"((add|sub|mul|div|mod)\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\))");

        std::smatch match;
        if (std::regex_search(result, match, func_re)) {
            std::string func = match[1].str();
            int a = std::stoi(match[2].str());
            int b = std::stoi(match[3].str());
            int res = 0;

            if (func == "add") {
                res = a + b;
            } else if (func == "sub") {
                res = a - b;
            } else if (func == "mul") {
                res = a * b;
            } else if (func == "div") {
                if (b != 0) {
                    res = a / b;
                } else if (diagnostics_) {
                    diagnostics_->addWarning(
                        "Division by zero in template expression, using 0",
                        template_ ? template_->file_path : "",
                        template_ ? template_->line_number : 0,
                        "math_error");
                }
            } else if (func == "mod") {
                if (b != 0) {
                    res = a % b;
                } else if (diagnostics_) {
                    diagnostics_->addWarning(
                        "Modulo by zero in template expression, using 0",
                        template_ ? template_->file_path : "",
                        template_ ? template_->line_number : 0,
                        "math_error");
                }
            }

            // Replace the function call with the result
            result.replace(match.position(), match.length(), std::to_string(res));
            changed = true;
        }
    }

    return result;
}

} // namespace slang_autos
