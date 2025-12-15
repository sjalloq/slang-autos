#include "slang-autos/Parser.h"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#include "slang-autos/Constants.h"
#include "slang-autos/SignalAggregator.h"  // For PortGrouping enum

// slang includes
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/parsing/Token.h"
#include "slang/text/SourceManager.h"

namespace slang_autos {

// ============================================================================
// Environment Variable Expansion
// ============================================================================

/// Helper to check if a character is valid in an environment variable name
static bool isEnvVarChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// Expand environment variables in a string.
/// Supports three forms: $VAR, ${VAR}, and $(VAR)
/// If a variable is not set, an error is reported via diagnostics.
std::string expandEnvironmentVariables(const std::string& input, DiagnosticCollector* diagnostics) {
    std::string result;
    result.reserve(input.size());

    const char* ptr = input.data();
    const char* end = ptr + input.size();

    while (ptr != end) {
        char c = *ptr++;
        if (c != '$') {
            result += c;
            continue;
        }

        // Found a '$', check what follows
        if (ptr == end) {
            // '$' at end of string, keep it literal
            result += '$';
            continue;
        }

        c = *ptr;
        if (c == '(' || c == '{') {
            // ${VAR} or $(VAR) form
            char endDelim = (c == '{') ? '}' : ')';
            ++ptr; // skip opening delimiter

            std::string varName;
            while (ptr != end && *ptr != endDelim) {
                varName += *ptr++;
            }

            if (ptr != end && *ptr == endDelim) {
                ++ptr; // skip closing delimiter
                // Look up the environment variable
                if (const char* value = std::getenv(varName.c_str())) {
                    result += value;
                } else if (diagnostics) {
                    diagnostics->addError(
                        "Environment variable '" + varName + "' is not set",
                        "", 0, "inline_config");
                }
            } else {
                // No closing delimiter found, keep literal
                result += '$';
                result += (endDelim == '}') ? '{' : '(';
                result += varName;
            }
        } else if (isEnvVarChar(c)) {
            // $VAR form (no delimiters)
            std::string varName;
            while (ptr != end && isEnvVarChar(*ptr)) {
                varName += *ptr++;
            }
            // Look up the environment variable
            if (const char* value = std::getenv(varName.c_str())) {
                result += value;
            } else if (diagnostics) {
                diagnostics->addError(
                    "Environment variable '" + varName + "' is not set",
                    "", 0, "inline_config");
            }
        } else {
            // Not a valid variable reference, keep the '$' literal
            result += '$';
            // Don't consume the character, it will be processed in the next iteration
        }
    }

    return result;
}

// ============================================================================
// TriviaCollector - SyntaxVisitor for collecting AUTO comments from trivia
// ============================================================================

struct TriviaCollector : public slang::syntax::SyntaxVisitor<TriviaCollector> {
    AutoParser& parser;
    const std::string& source_text;
    const std::string& file_path;

    TriviaCollector(AutoParser& p, const std::string& src, const std::string& path)
        : parser(p), source_text(src), file_path(path) {}

    void visitToken(slang::parsing::Token token) {
        auto token_offset = token.location().offset();

        // Calculate trivia start by summing trivia lengths
        size_t trivia_total_len = 0;
        for (const auto& trivia : token.trivia()) {
            trivia_total_len += trivia.getRawText().length();
        }
        size_t current_offset = token_offset - trivia_total_len;

        for (const auto& trivia : token.trivia()) {
            std::string_view raw_text = trivia.getRawText();

            if (trivia.kind == slang::parsing::TriviaKind::BlockComment) {
                size_t offset = current_offset;

                // Calculate line/column from offset
                size_t line = 1;
                size_t col = 1;
                if (!source_text.empty() && offset < source_text.length()) {
                    // Count newlines to get line number
                    for (size_t i = 0; i < offset; ++i) {
                        if (source_text[i] == '\n') {
                            ++line;
                        }
                    }
                    // Find column
                    auto last_newline = source_text.rfind('\n', offset);
                    col = (last_newline == std::string::npos) ? offset + 1 : offset - last_newline;
                }

                // Check for AUTO_TEMPLATE
                if (raw_text.find("AUTO_TEMPLATE") != std::string_view::npos) {
                    parser.processBlockComment(raw_text, file_path, line, col, offset, "AUTO_TEMPLATE");
                }
                // Check for AUTOINST
                else if (raw_text.find("AUTOINST") != std::string_view::npos) {
                    parser.processBlockComment(raw_text, file_path, line, col, offset, "AUTOINST");
                }
                // Check for AUTOLOGIC
                else if (raw_text.find("AUTOLOGIC") != std::string_view::npos) {
                    parser.processBlockComment(raw_text, file_path, line, col, offset, "AUTOLOGIC");
                }
                // Check for AUTOPORTS
                else if (raw_text.find("AUTOPORTS") != std::string_view::npos) {
                    parser.processBlockComment(raw_text, file_path, line, col, offset, "AUTOPORTS");
                }
            }

            current_offset += raw_text.length();
        }
    }
};

// ============================================================================
// Re2TemplateParser Implementation
// ============================================================================

Re2TemplateParser::Re2TemplateParser(DiagnosticCollector* diagnostics)
    : diagnostics_(diagnostics) {
}

std::optional<AutoTemplate> Re2TemplateParser::parseTemplate(
    std::string_view text,
    const std::string& file_path,
    size_t line,
    size_t offset) {

    // Pattern: /* module_name AUTO_TEMPLATE ["instance_pattern"]
    //             rule1
    //             rule2
    //          */
    // Instance pattern is optional - when omitted, template applies to all instances
    static const std::regex header_re(
        R"re(/\*\s*(\w+)\s+AUTO_TEMPLATE(?:\s+"([^"]*)")?\s*)re",
        std::regex::multiline);

    static const std::regex rule_re(
        R"(^\s*(\S+)\s*=>\s*(.+?)\s*$)",
        std::regex::multiline);

    std::string text_str(text);

    // Match header
    std::smatch header_match;
    if (!std::regex_search(text_str, header_match, header_re)) {
        // Not a valid AUTO_TEMPLATE
        if (text.find("AUTO_TEMPLATE") != std::string_view::npos && diagnostics_) {
            diagnostics_->addWarning(
                "Malformed AUTO_TEMPLATE: missing or invalid header format. "
                "Expected: /* module_name AUTO_TEMPLATE [\"instance_pattern\"]",
                file_path, line, "template_syntax");
        }
        return std::nullopt;
    }

    AutoTemplate tmpl;
    tmpl.module_name = header_match[1].str();
    tmpl.instance_pattern = header_match[2].str();
    tmpl.file_path = file_path;
    tmpl.line_number = line;
    tmpl.source_offset = offset;

    // Validate instance pattern is valid regex
    if (!tmpl.instance_pattern.empty()) {
        try {
            std::regex test_re(tmpl.instance_pattern);
        } catch (const std::regex_error& e) {
            if (diagnostics_) {
                diagnostics_->addWarning(
                    "Invalid regex in AUTO_TEMPLATE instance pattern '" +
                    tmpl.instance_pattern + "': " + e.what(),
                    file_path, line, "template_regex");
            }
        }
    }

    // Parse rules from rest of comment
    std::string rest = text_str.substr(header_match.position() + header_match.length());

    // Remove trailing */
    auto close_pos = rest.rfind("*/");
    if (close_pos != std::string::npos) {
        rest = rest.substr(0, close_pos);
    }

    // Find all rules
    std::sregex_iterator rule_it(rest.begin(), rest.end(), rule_re);
    std::sregex_iterator rule_end;

    for (; rule_it != rule_end; ++rule_it) {
        std::string port_pattern = (*rule_it)[1].str();
        std::string signal_expr = (*rule_it)[2].str();

        // Strip trailing comma from signal expression (verilog-mode compatibility)
        if (!signal_expr.empty() && signal_expr.back() == ',') {
            signal_expr.pop_back();
            // Also trim any trailing whitespace that was before the comma
            while (!signal_expr.empty() && std::isspace(signal_expr.back())) {
                signal_expr.pop_back();
            }
        }

        // Validate port pattern is valid regex
        try {
            std::regex test_re(port_pattern);
        } catch (const std::regex_error& e) {
            if (diagnostics_) {
                diagnostics_->addWarning(
                    "Invalid regex in template rule port pattern '" +
                    port_pattern + "': " + e.what(),
                    file_path, line, "template_regex");
            }
            continue;  // Skip this rule
        }

        tmpl.rules.emplace_back(port_pattern, signal_expr);
    }

    // Warn if template has no rules
    if (tmpl.rules.empty() && diagnostics_) {
        diagnostics_->addWarning(
            "AUTO_TEMPLATE for module '" + tmpl.module_name + "' has no rules",
            file_path, line, "template_empty");
    }

    return tmpl;
}

// ============================================================================
// AutoParser Implementation
// ============================================================================

AutoParser::AutoParser(DiagnosticCollector* diagnostics)
    : template_parser_(std::make_unique<Re2TemplateParser>(diagnostics))
    , diagnostics_(diagnostics) {
}

void AutoParser::parseFile(const std::filesystem::path& file) {
    std::ifstream ifs(file);
    if (!ifs) {
        if (diagnostics_) {
            diagnostics_->addError("Failed to open file: " + file.string());
        }
        return;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    parseText(buffer.str(), file.string());
}

void AutoParser::parseText(std::string_view text, const std::string& file_path) {
    processTree(std::string(text), file_path);
}

void AutoParser::processTree(const std::string& source_text, const std::string& file_path) {
    // Parse with slang to get syntax tree
    auto tree = slang::syntax::SyntaxTree::fromText(source_text);

    // Use SyntaxVisitor with visitToken() - cleaner and more efficient than manual recursion
    TriviaCollector collector(*this, source_text, file_path);
    tree->root().visit(collector);
}

void AutoParser::processBlockComment(
    std::string_view raw_text,
    const std::string& file_path,
    size_t line,
    size_t col,
    size_t offset,
    const std::string& comment_type) {

    if (comment_type == "AUTO_TEMPLATE") {
        auto tmpl = template_parser_->parseTemplate(raw_text, file_path, line, offset);
        if (tmpl) {
            templates_.push_back(std::move(*tmpl));
        }
    }
    else if (comment_type == "AUTOINST") {
        auto autoinst = parseAutoInst(raw_text, file_path, line, col, offset);
        if (autoinst) {
            autoinsts_.push_back(std::move(*autoinst));
        }
    }
    else if (comment_type == "AUTOLOGIC") {
        AutoLogic autologic;
        autologic.file_path = file_path;
        autologic.line_number = line;
        autologic.column = col;
        autologic.source_offset = offset;
        autologic.end_offset = offset + raw_text.length();
        autologics_.push_back(std::move(autologic));
    }
    else if (comment_type == "AUTOPORTS") {
        AutoPorts autoports;
        autoports.file_path = file_path;
        autoports.line_number = line;
        autoports.column = col;
        autoports.source_offset = offset;
        autoports.end_offset = offset + raw_text.length();
        autoports_.push_back(std::move(autoports));
    }
}

std::optional<AutoInst> AutoParser::parseAutoInst(
    std::string_view text,
    const std::string& file_path,
    size_t line,
    size_t column,
    size_t offset) {

    // Pattern: /*AUTOINST*/ or /*AUTOINST("filter")*/
    static const std::regex autoinst_re(
        R"re(/\*AUTOINST(?:\s*\(\s*"([^"]*)"\s*\))?\s*\*/)re");

    std::string text_str(text);
    std::smatch match;

    if (!std::regex_match(text_str, match, autoinst_re)) {
        if (text.find("AUTOINST") != std::string_view::npos && diagnostics_) {
            diagnostics_->addWarning(
                "Malformed AUTOINST comment",
                file_path, line, "autoinst_syntax");
        }
        return std::nullopt;
    }

    AutoInst autoinst;
    autoinst.file_path = file_path;
    autoinst.line_number = line;
    autoinst.column = column;
    autoinst.source_offset = offset;
    autoinst.end_offset = offset + text.length();

    // Extract optional filter pattern
    if (match[1].matched) {
        autoinst.filter_pattern = match[1].str();

        // Validate filter is valid regex
        try {
            std::regex test_re(*autoinst.filter_pattern);
        } catch (const std::regex_error& e) {
            if (diagnostics_) {
                diagnostics_->addWarning(
                    "Invalid regex in AUTOINST filter pattern '" +
                    *autoinst.filter_pattern + "': " + e.what(),
                    file_path, line, "autoinst_regex");
            }
        }
    }

    return autoinst;
}

void AutoParser::clear() {
    templates_.clear();
    autoinsts_.clear();
    autologics_.clear();
    autoports_.clear();
}

void AutoParser::setTemplateParser(std::unique_ptr<ITemplateParser> parser) {
    template_parser_ = std::move(parser);
}

// ============================================================================
// Inline Configuration Parser
// ============================================================================

InlineConfig parseInlineConfig(const std::string& content, const std::string& file_path, DiagnosticCollector* diagnostics) {
    InlineConfig config;

    // Determine base directory for resolving relative paths
    std::filesystem::path base_dir;
    if (!file_path.empty()) {
        base_dir = std::filesystem::absolute(file_path).parent_path();
    } else {
        base_dir = std::filesystem::current_path();
    }

    // Helper to emit warnings for invalid values
    auto warnInvalidValue = [&](const std::string& key, const std::string& value,
                                 const std::string& valid_values) {
        if (diagnostics) {
            diagnostics->addWarning(
                "Invalid value '" + value + "' for slang-autos-" + key +
                ". Valid values: " + valid_values,
                "", 0, "inline_config");
        }
    };

    // Helper to validate a directory path exists
    auto validateDirectory = [&](const std::string& key, const std::string& dir) -> bool {
        std::filesystem::path resolved = (base_dir / dir).lexically_normal();
        if (!std::filesystem::exists(resolved)) {
            if (diagnostics) {
                diagnostics->addWarning(
                    "Directory '" + dir + "' for slang-autos-" + key +
                    " does not exist (resolved to '" + resolved.string() + "')",
                    file_path, 0, "inline_config");
            }
            return false;
        }
        if (!std::filesystem::is_directory(resolved)) {
            if (diagnostics) {
                diagnostics->addWarning(
                    "Path '" + dir + "' for slang-autos-" + key +
                    " is not a directory (resolved to '" + resolved.string() + "')",
                    file_path, 0, "inline_config");
            }
            return false;
        }
        return true;
    };

    // Pattern: // slang-autos-KEY: VALUES
    // We look for single-line comments with the slang-autos- prefix
    // Note: Key pattern includes hyphens for options like "resolved-ranges"
    static const std::regex config_re(
        R"re(//\s*slang-autos-([\w-]+)\s*:\s*(.+)$)re",
        std::regex::multiline);

    auto begin = std::sregex_iterator(content.begin(), content.end(), config_re);
    auto end = std::sregex_iterator();

    for (std::sregex_iterator it = begin; it != end; ++it) {
        std::smatch match = *it;
        std::string key = match[1].str();
        std::string value = match[2].str();

        // Trim trailing whitespace from value
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }

        // Expand environment variables in the value
        value = expandEnvironmentVariables(value, diagnostics);

        if (key == "libdir") {
            // Split value by whitespace
            std::istringstream iss(value);
            std::string dir;
            while (iss >> dir) {
                validateDirectory(key, dir);
                config.libdirs.push_back(dir);
            }
        } else if (key == "libext") {
            // Split value by whitespace
            std::istringstream iss(value);
            std::string ext;
            while (iss >> ext) {
                if (!ext.empty() && ext[0] != '.') {
                    if (diagnostics) {
                        diagnostics->addWarning(
                            "Extension '" + ext + "' does not start with '.', adding it",
                            "", 0, "inline_config");
                    }
                    ext = "." + ext;
                }
                config.libext.push_back(ext);
            }
        } else if (key == "incdir") {
            // Split value by whitespace
            std::istringstream iss(value);
            std::string dir;
            while (iss >> dir) {
                validateDirectory(key, dir);
                config.incdirs.push_back(dir);
            }
        } else if (key == "grouping") {
            if (value == "alphabetical" || value == "alpha") {
                config.grouping = PortGrouping::Alphabetical;
            } else if (value == "direction" || value == "bydirection") {
                config.grouping = PortGrouping::ByDirection;
            } else {
                warnInvalidValue(key, value, "alphabetical, alpha, direction, bydirection");
            }
        } else if (key == "indent") {
            if (value == "tab") {
                config.indent = -1;
            } else {
                try {
                    int indent_val = std::stoi(value);
                    if (indent_val < 0 || indent_val > 16) {
                        warnInvalidValue(key, value, "tab, or 0-16");
                    } else {
                        config.indent = indent_val;
                    }
                } catch (...) {
                    warnInvalidValue(key, value, "tab, or a number (0-16)");
                }
            }
        } else if (key == "alignment") {
            if (value == "true" || value == "1" || value == "yes") {
                config.alignment = true;
            } else if (value == "false" || value == "0" || value == "no") {
                config.alignment = false;
            } else {
                warnInvalidValue(key, value, "true, false, yes, no, 1, 0");
            }
        } else if (key == "strictness") {
            if (value == "strict") {
                config.strictness = StrictnessMode::Strict;
            } else if (value == "lenient") {
                config.strictness = StrictnessMode::Lenient;
            } else {
                warnInvalidValue(key, value, "strict, lenient");
            }
        } else if (key == "resolved-ranges") {
            if (value == "true" || value == "1" || value == "yes") {
                config.resolved_ranges = true;
            } else if (value == "false" || value == "0" || value == "no") {
                config.resolved_ranges = false;
            } else {
                warnInvalidValue(key, value, "true, false, yes, no, 1, 0");
            }
        } else {
            // Unknown key - warn and store as custom option
            if (diagnostics) {
                diagnostics->addWarning(
                    "Unknown inline config key 'slang-autos-" + key + "'. "
                    "Valid keys: libdir, libext, incdir, grouping, indent, alignment, strictness, resolved-ranges",
                    "", 0, "inline_config");
            }
            config.custom_options[key] = value;
        }
    }

    return config;
}

} // namespace slang_autos
