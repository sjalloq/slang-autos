#include "slang-autos/TemplateMatcher.h"

#include <regex>
#include <unordered_map>

namespace slang_autos {

namespace {

// Special value mappings
const std::unordered_map<std::string, std::string> SPECIAL_VALUE_MAP = {
    {"_", ""},       // Unconnected
    {"'0", "1'b0"},  // Constant 0
    {"'1", "1'b1"},  // Constant 1
    {"'z", "1'bz"},  // High impedance
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
    } catch (const std::regex_error&) {
        // Invalid regex - treat as literal match
        return instance_name == template_->instance_pattern;
    }
}

MatchResult TemplateMatcher::matchPort(const PortInfo& port) {
    if (!template_) {
        // No template - default to port name
        return MatchResult(port.name);
    }

    // Try each rule in order (first match wins)
    for (const auto& rule : template_->rules) {
        try {
            std::regex pattern(rule.port_pattern);
            std::smatch match;

            if (std::regex_match(port.name, match, pattern)) {
                // Extract port captures
                std::vector<std::string> port_captures;
                for (size_t i = 1; i < match.size(); ++i) {
                    port_captures.push_back(match[i].str());
                }

                // Apply substitution
                std::string signal_name = substitute(rule.signal_expr, port, port_captures);
                return MatchResult(signal_name, &rule);
            }
        } catch (const std::regex_error&) {
            // Invalid regex - try literal match
            if (rule.port_pattern == port.name) {
                std::string signal_name = substitute(rule.signal_expr, port, {});
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

} // namespace slang_autos
