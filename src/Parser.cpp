#include "slang-autos/Parser.h"

#include <fstream>
#include <regex>
#include <sstream>

// slang includes
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/parsing/Token.h"
#include "slang/text/SourceManager.h"

namespace slang_autos {

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
                // Check for AUTOWIRE
                else if (raw_text.find("AUTOWIRE") != std::string_view::npos) {
                    parser.processBlockComment(raw_text, file_path, line, col, offset, "AUTOWIRE");
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

    // Pattern: /* module_name AUTO_TEMPLATE "instance_pattern"
    //             rule1
    //             rule2
    //          */
    static const std::regex header_re(
        R"re(/\*\s*(\w+)\s+AUTO_TEMPLATE\s+"([^"]*)"\s*)re",
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
                "Expected: /* module_name AUTO_TEMPLATE \"instance_pattern\"",
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
    else if (comment_type == "AUTOWIRE") {
        auto autowire = parseAutoWire(raw_text, file_path, line, col, offset);
        if (autowire) {
            autowires_.push_back(std::move(*autowire));
        }
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

std::optional<AutoWire> AutoParser::parseAutoWire(
    std::string_view text,
    const std::string& file_path,
    size_t line,
    size_t column,
    size_t offset) {

    static const std::regex autowire_re(R"(/\*AUTOWIRE\*/)");

    std::string text_str(text);
    if (!std::regex_match(text_str, autowire_re)) {
        return std::nullopt;
    }

    AutoWire autowire;
    autowire.file_path = file_path;
    autowire.line_number = line;
    autowire.column = column;
    autowire.source_offset = offset;
    autowire.end_offset = offset + text.length();

    return autowire;
}

const AutoTemplate* AutoParser::getTemplateForModule(
    const std::string& module_name,
    size_t before_line) const {

    const AutoTemplate* best = nullptr;
    size_t best_line = 0;

    for (const auto& tmpl : templates_) {
        if (tmpl.module_name == module_name &&
            tmpl.line_number < before_line &&
            tmpl.line_number > best_line) {
            best = &tmpl;
            best_line = tmpl.line_number;
        }
    }

    return best;
}

void AutoParser::clear() {
    templates_.clear();
    autoinsts_.clear();
    autowires_.clear();
}

void AutoParser::setTemplateParser(std::unique_ptr<ITemplateParser> parser) {
    template_parser_ = std::move(parser);
}

} // namespace slang_autos
