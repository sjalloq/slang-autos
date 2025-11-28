#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Diagnostics.h"
#include "Expander.h"
#include "Parser.h"
#include "Writer.h"

// Forward declarations for slang types
namespace slang {
namespace driver { class Driver; }
namespace ast { class Compilation; }
}

namespace slang_autos {

/// Strictness mode for error handling
enum class StrictnessMode {
    Strict,     ///< Error on missing modules, undefined parameters
    Lenient     ///< Warn and continue with best-effort expansion
};

/// Result of expanding a single file
struct ExpansionResult {
    std::string original_content;   ///< Original file content
    std::string modified_content;   ///< Content after expansion
    std::vector<Replacement> replacements;  ///< All replacements made
    int autoinst_count = 0;         ///< Number of AUTOINSTs expanded
    int autowire_count = 0;         ///< Number of AUTOWIREs expanded
    bool success = true;            ///< false if fatal errors occurred

    /// Check if any changes were made
    [[nodiscard]] bool hasChanges() const {
        return original_content != modified_content;
    }
};

/// Configuration options for AutosTool
struct AutosToolOptions {
    StrictnessMode strictness = StrictnessMode::Lenient;
    bool alignment = true;      ///< Align port names in output
    std::string indent = "    ";///< Indentation string
    int verbosity = 0;          ///< 0=quiet, 1=normal, 2=verbose, 3=debug
};

/// Main orchestrator for AUTO macro expansion.
/// Coordinates parsing, template matching, expansion, and file writing.
class AutosTool {
public:
    using Options = AutosToolOptions;

    AutosTool();
    explicit AutosTool(const Options& options);
    ~AutosTool();

    // Non-copyable, movable
    AutosTool(const AutosTool&) = delete;
    AutosTool& operator=(const AutosTool&) = delete;
    AutosTool(AutosTool&&) noexcept;
    AutosTool& operator=(AutosTool&&) noexcept;

    /// Load design files with slang CLI arguments.
    /// Supports standard EDA arguments: -f, -y, +libext+, +incdir+, +define+
    /// @param args Command line arguments (file paths and options)
    /// @return true if loading succeeded
    bool loadWithArgs(const std::vector<std::string>& args);

    /// Expand all AUTO macros in a file.
    /// @param file Path to the file to expand
    /// @param dry_run If true, don't modify the file
    /// @return Expansion result with original and modified content
    [[nodiscard]] ExpansionResult expandFile(
        const std::filesystem::path& file,
        bool dry_run = false);

    /// Get the diagnostics collector
    [[nodiscard]] DiagnosticCollector& diagnostics() { return diagnostics_; }
    [[nodiscard]] const DiagnosticCollector& diagnostics() const { return diagnostics_; }

    /// Get current options
    [[nodiscard]] const Options& options() const { return options_; }

    /// Set options
    void setOptions(const Options& options) { options_ = options; }

private:
    /// Extract port information for a module from compilation
    std::vector<PortInfo> getModulePorts(const std::string& module_name);

    /// Expand a single AUTOINST and return the replacement + expanded signals
    std::optional<std::pair<Replacement, std::vector<ExpandedSignal>>>
    expandAutoInst(
        const std::string& content,
        const AutoInst& autoinst,
        const AutoParser& parser);

    /// Expand a single AUTOWIRE and return the replacement
    std::optional<Replacement> expandAutoWire(
        const std::string& content,
        const AutoWire& autowire,
        const std::vector<ExpandedSignal>& all_signals);

    Options options_;
    DiagnosticCollector diagnostics_;
    std::unique_ptr<slang::driver::Driver> driver_;
    std::unique_ptr<slang::ast::Compilation> compilation_;

    /// Cache for module port lookups (avoids repeated AST traversal)
    std::unordered_map<std::string, std::vector<PortInfo>> port_cache_;
};

} // namespace slang_autos
