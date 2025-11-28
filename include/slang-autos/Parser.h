#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics.h"

namespace slang_autos {

/// A single port mapping rule in a template.
/// Maps port names (via pattern) to signal expressions.
struct TemplateRule {
    std::string port_pattern;   ///< RE2 regex pattern matching port names
    std::string signal_expr;    ///< Signal expression with substitutions ($1, %1, etc.)
    size_t line_number = 0;     ///< Line where rule was defined

    TemplateRule() = default;
    TemplateRule(std::string pattern, std::string expr, size_t line = 0)
        : port_pattern(std::move(pattern))
        , signal_expr(std::move(expr))
        , line_number(line) {}
};

/// Represents an AUTO_TEMPLATE definition.
/// Templates define how ports are mapped to signals for matching instances.
struct AutoTemplate {
    std::string module_name;        ///< Module type this template applies to
    std::string instance_pattern;   ///< RE2 regex for instance name captures
    std::vector<TemplateRule> rules;///< Port-to-signal mapping rules
    std::string file_path;          ///< Source file containing the template
    size_t line_number = 0;         ///< Line number where template starts
    size_t source_offset = 0;       ///< Byte offset in source

    AutoTemplate() = default;
};

/// Represents an AUTOINST comment location.
/// Marks where automatic port instantiation should be expanded.
struct AutoInst {
    std::string file_path;
    size_t line_number = 0;
    size_t column = 0;
    size_t source_offset = 0;       ///< Byte offset where /*AUTOINST*/ starts
    size_t end_offset = 0;          ///< Byte offset where /*AUTOINST*/ ends
    std::optional<std::string> filter_pattern;  ///< Optional filter /*AUTOINST("pattern")*/

    AutoInst() = default;
};

/// Represents an AUTOWIRE comment location.
/// Marks where automatic wire declarations should be generated.
struct AutoWire {
    std::string file_path;
    size_t line_number = 0;
    size_t column = 0;
    size_t source_offset = 0;       ///< Byte offset where /*AUTOWIRE*/ starts
    size_t end_offset = 0;          ///< Byte offset where /*AUTOWIRE*/ ends

    AutoWire() = default;
};

/// Abstract interface for template parsers.
/// Allows different syntax implementations (RE2 vs verilog-mode).
class ITemplateParser {
public:
    virtual ~ITemplateParser() = default;

    /// Parse an AUTO_TEMPLATE from comment text.
    /// @param text Raw comment text including /* */
    /// @param file_path Source file path for diagnostics
    /// @param line Line number where comment starts
    /// @param offset Byte offset in source
    /// @return Parsed template or nullopt if not a valid template
    virtual std::optional<AutoTemplate> parseTemplate(
        std::string_view text,
        const std::string& file_path,
        size_t line,
        size_t offset) = 0;
};

/// RE2-based template parser implementing sv-autos syntax.
/// Syntax: /* module AUTO_TEMPLATE "instance_pattern"
///            port_pattern => signal_expr
///         */
class Re2TemplateParser : public ITemplateParser {
public:
    explicit Re2TemplateParser(DiagnosticCollector* diagnostics = nullptr);

    std::optional<AutoTemplate> parseTemplate(
        std::string_view text,
        const std::string& file_path,
        size_t line,
        size_t offset) override;

private:
    DiagnosticCollector* diagnostics_;
};

/// Parser for AUTO comments in SystemVerilog source files.
/// Uses slang's trivia API to find block comments and parses
/// AUTO_TEMPLATE, AUTOINST, and AUTOWIRE comments.
class AutoParser {
public:
    explicit AutoParser(DiagnosticCollector* diagnostics = nullptr);

    /// Parse a file for AUTO comments
    void parseFile(const std::filesystem::path& file);

    /// Parse text for AUTO comments
    void parseText(std::string_view text, const std::string& file_path = "");

    /// Get all parsed templates
    [[nodiscard]] const std::vector<AutoTemplate>& templates() const { return templates_; }

    /// Get all parsed AUTOINST comments
    [[nodiscard]] const std::vector<AutoInst>& autoinsts() const { return autoinsts_; }

    /// Get all parsed AUTOWIRE comments
    [[nodiscard]] const std::vector<AutoWire>& autowires() const { return autowires_; }

    /// Find the nearest template for a module, searching backward from a line.
    /// @param module_name Module type to find template for
    /// @param before_line Only consider templates before this line
    /// @return Pointer to matching template or nullptr
    [[nodiscard]] const AutoTemplate* getTemplateForModule(
        const std::string& module_name,
        size_t before_line) const;

    /// Clear all parsed results
    void clear();

    /// Set the template parser implementation
    void setTemplateParser(std::unique_ptr<ITemplateParser> parser);

private:
    /// Parse AUTOINST from comment text
    std::optional<AutoInst> parseAutoInst(
        std::string_view text,
        const std::string& file_path,
        size_t line,
        size_t column,
        size_t offset);

    /// Parse AUTOWIRE from comment text
    std::optional<AutoWire> parseAutoWire(
        std::string_view text,
        const std::string& file_path,
        size_t line,
        size_t column,
        size_t offset);

    /// Process the syntax tree looking for AUTO comments in trivia
    void processTree(const std::string& source_text, const std::string& file_path);

    std::unique_ptr<ITemplateParser> template_parser_;
    DiagnosticCollector* diagnostics_;
    std::vector<AutoTemplate> templates_;
    std::vector<AutoInst> autoinsts_;
    std::vector<AutoWire> autowires_;
};

} // namespace slang_autos
