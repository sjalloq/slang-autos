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

// StrictnessMode is defined in Diagnostics.h

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
/// NOTE: In production, values are set from MergedConfig via toToolOptions().
///       Defaults here are for direct use (e.g., tests).
struct AutosToolOptions {
    StrictnessMode strictness = StrictnessMode::Lenient;
    bool alignment = true;
    std::string indent = "  ";  ///< Default 2 spaces (matches MergedConfig)
    int verbosity = 1;
    bool single_unit = true;
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

    /// Set a pre-created compilation (alternative to loadWithArgs).
    /// Used when the driver is managed externally (e.g., by main.cpp).
    void setCompilation(std::unique_ptr<slang::ast::Compilation> compilation);

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

    /// Set pre-parsed inline config for a file (avoids double-parsing)
    void setInlineConfig(const std::filesystem::path& file, const InlineConfig& config);

private:
    /// Get inline config for a file (returns empty config if not set)
    [[nodiscard]] InlineConfig getInlineConfig(const std::filesystem::path& file) const;
    /// Extract port information for a module from compilation
    std::vector<PortInfo> getModulePorts(const std::string& module_name);

    Options options_;
    DiagnosticCollector diagnostics_;
    std::unique_ptr<slang::driver::Driver> driver_;
    std::unique_ptr<slang::ast::Compilation> compilation_;

    /// Cache for module port lookups (avoids repeated AST traversal)
    std::unordered_map<std::string, std::vector<PortInfo>> port_cache_;

    /// Pre-parsed inline configs per file (set by main.cpp, avoids double-parsing)
    std::unordered_map<std::string, InlineConfig> inline_configs_;
};

} // namespace slang_autos
