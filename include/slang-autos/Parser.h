#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Diagnostics.h"

namespace slang_autos {

// Forward declaration
enum class PortGrouping;

/// Inline configuration parsed from file comments.
/// Supports local variables in comments:
///   // slang-autos-libdir: ./lib ./lib2
///   // slang-autos-libext: .v .sv
///   // slang-autos-incdir: ./include
///   // slang-autos-grouping: alphabetical
///   // slang-autos-indent: 4
///   // slang-autos-alignment: true
///   // slang-autos-strictness: lenient
struct InlineConfig {
    std::vector<std::string> libdirs;   ///< Library directories (-y equivalents)
    std::vector<std::string> libext;    ///< File extensions (+libext+ equivalents)
    std::vector<std::string> incdirs;   ///< Include directories (+incdir+ equivalents)
    std::optional<PortGrouping> grouping; ///< Port grouping preference
    std::optional<int> indent;          ///< Indentation spaces (-1 for tab)
    std::optional<bool> alignment;      ///< Port name alignment
    std::optional<StrictnessMode> strictness; ///< Strictness mode
    std::unordered_map<std::string, std::string> custom_options; ///< Other options

    /// Check if any configuration was found
    [[nodiscard]] bool empty() const {
        return libdirs.empty() && libext.empty() && incdirs.empty() &&
               !grouping.has_value() && !indent.has_value() &&
               !alignment.has_value() && !strictness.has_value() &&
               custom_options.empty();
    }
};

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

/// Represents an AUTOREG comment location.
/// Marks where automatic reg declarations should be generated.
struct AutoReg {
    std::string file_path;
    size_t line_number = 0;
    size_t column = 0;
    size_t source_offset = 0;
    size_t end_offset = 0;

    AutoReg() = default;
};

/// Represents an AUTOPORTS comment location (ANSI-style port list).
/// Generates combined input/output/inout declarations inside module port list.
struct AutoPorts {
    std::string file_path;
    size_t line_number = 0;
    size_t column = 0;
    size_t source_offset = 0;
    size_t end_offset = 0;

    AutoPorts() = default;
};

/// Represents an AUTOINPUT comment location.
/// Marks where automatic input declarations should be generated.
struct AutoInput {
    std::string file_path;
    size_t line_number = 0;
    size_t column = 0;
    size_t source_offset = 0;
    size_t end_offset = 0;

    AutoInput() = default;
};

/// Represents an AUTOOUTPUT comment location.
/// Marks where automatic output declarations should be generated.
struct AutoOutput {
    std::string file_path;
    size_t line_number = 0;
    size_t column = 0;
    size_t source_offset = 0;
    size_t end_offset = 0;

    AutoOutput() = default;
};

/// Represents an AUTOINOUT comment location.
/// Marks where automatic inout declarations should be generated.
struct AutoInout {
    std::string file_path;
    size_t line_number = 0;
    size_t column = 0;
    size_t source_offset = 0;
    size_t end_offset = 0;

    AutoInout() = default;
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
/// AUTO_TEMPLATE, AUTOINST, AUTOWIRE, AUTOREG, AUTOPORTS, etc.
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

    /// Get all parsed AUTOREG comments
    [[nodiscard]] const std::vector<AutoReg>& autoregs() const { return autoregs_; }

    /// Get all parsed AUTOPORTS comments
    [[nodiscard]] const std::vector<AutoPorts>& autoports() const { return autoports_; }

    /// Get all parsed AUTOINPUT comments
    [[nodiscard]] const std::vector<AutoInput>& autoinputs() const { return autoinputs_; }

    /// Get all parsed AUTOOUTPUT comments
    [[nodiscard]] const std::vector<AutoOutput>& autooutputs() const { return autooutputs_; }

    /// Get all parsed AUTOINOUT comments
    [[nodiscard]] const std::vector<AutoInout>& autoinouts() const { return autoinouts_; }

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

    /// Process a block comment containing AUTO directives.
    /// Called by TriviaCollector during syntax tree traversal.
    void processBlockComment(
        std::string_view raw_text,
        const std::string& file_path,
        size_t line,
        size_t col,
        size_t offset,
        const std::string& comment_type);

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
    std::vector<AutoReg> autoregs_;
    std::vector<AutoPorts> autoports_;
    std::vector<AutoInput> autoinputs_;
    std::vector<AutoOutput> autooutputs_;
    std::vector<AutoInout> autoinouts_;
};

// ============================================================================
// Environment Variable Expansion
// ============================================================================

/// Expand environment variables in a string.
/// Supports three forms: $VAR, ${VAR}, and $(VAR)
/// @param input String containing environment variable references
/// @param diagnostics Optional collector for errors (undefined variables)
/// @return String with all environment variables expanded (undefined vars become empty)
[[nodiscard]] std::string expandEnvironmentVariables(
    const std::string& input,
    DiagnosticCollector* diagnostics = nullptr);

// ============================================================================
// Inline Configuration Parser
// ============================================================================

/// Parse inline configuration from file content.
/// Searches for comments matching the pattern: // slang-autos-KEY: VALUES
/// Typically placed at the end of a file, similar to verilog-mode's local variables.
/// @param content File content to parse
/// @param file_path Path to source file (for resolving relative paths in validation)
/// @param diagnostics Optional collector for warnings about invalid values
/// @return Parsed configuration (may be empty if no config comments found)
[[nodiscard]] InlineConfig parseInlineConfig(
    const std::string& content,
    const std::string& file_path = "",
    DiagnosticCollector* diagnostics = nullptr);

} // namespace slang_autos
