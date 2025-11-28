#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace slang_autos {

/// Strictness mode for error handling
enum class StrictnessMode {
    Strict,     ///< Error on missing modules, undefined parameters
    Lenient     ///< Warn and continue with best-effort expansion
};

/// Diagnostic severity level
enum class DiagnosticLevel {
    Warning,
    Error
};

/// A single diagnostic message with location information
struct Diagnostic {
    DiagnosticLevel level;
    std::string message;
    std::string file_path;
    size_t line_number = 0;
    std::string type;  // Category: "template_syntax", "unresolved_capture", etc.

    Diagnostic(DiagnosticLevel lvl, std::string msg,
               std::string file = "", size_t line = 0,
               std::string diag_type = "")
        : level(lvl)
        , message(std::move(msg))
        , file_path(std::move(file))
        , line_number(line)
        , type(std::move(diag_type)) {}
};

/// Collects warnings and errors without throwing exceptions.
/// Used throughout the tool to accumulate diagnostics for later reporting.
class DiagnosticCollector {
public:
    DiagnosticCollector() = default;

    /// Add a warning diagnostic
    void addWarning(const std::string& msg,
                    const std::string& file = "",
                    size_t line = 0,
                    const std::string& type = "");

    /// Add an error diagnostic
    void addError(const std::string& msg,
                  const std::string& file = "",
                  size_t line = 0,
                  const std::string& type = "");

    /// Get all collected diagnostics
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    /// Check if any errors were recorded
    [[nodiscard]] bool hasErrors() const { return error_count_ > 0; }

    /// Get count of errors
    [[nodiscard]] size_t errorCount() const { return error_count_; }

    /// Get count of warnings
    [[nodiscard]] size_t warningCount() const { return warning_count_; }

    /// Clear all diagnostics
    void clear();

    /// Format all diagnostics as a string for output
    [[nodiscard]] std::string format() const;

private:
    std::vector<Diagnostic> diagnostics_;
    size_t error_count_ = 0;
    size_t warning_count_ = 0;
};

} // namespace slang_autos
