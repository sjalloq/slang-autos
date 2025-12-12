#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Diagnostics.h"

namespace slang_autos {

// Forward declaration
struct InlineConfig;

/// Configuration loaded from a .slang-autos.toml file.
/// All fields are optional - missing values use defaults during merge.
struct FileConfig {
    // [library] section
    std::optional<std::vector<std::string>> libdirs;   ///< -y equivalents
    std::optional<std::vector<std::string>> libext;    ///< +libext+ equivalents
    std::optional<std::vector<std::string>> incdirs;   ///< +incdir+ equivalents

    // [formatting] section
    std::optional<int> indent;          ///< Number of spaces (or -1 for tab)
    std::optional<bool> alignment;      ///< Align port names

    // [behavior] section
    std::optional<StrictnessMode> strictness;
    std::optional<int> verbosity;       ///< 0=quiet, 1=normal, 2=verbose
    std::optional<bool> single_unit;    ///< Treat all files as single compilation unit

    /// Check if any configuration was loaded
    [[nodiscard]] bool empty() const {
        return !libdirs && !libext && !incdirs &&
               !indent && !alignment &&
               !strictness && !verbosity && !single_unit;
    }
};

/// Tracks which CLI options were explicitly specified (vs using defaults).
/// Used for priority-based merging.
struct CliFlags {
    bool has_strictness = false;
    bool has_alignment = false;
    bool has_indent = false;
    bool has_verbosity = false;
    bool has_single_unit = false;
};

/// Final merged configuration with all values resolved.
/// Priority: CLI > inline > file > defaults
struct MergedConfig {
    // Library paths (additive from all sources)
    std::vector<std::string> libdirs;
    std::vector<std::string> libext;
    std::vector<std::string> incdirs;

    // Formatting (override semantics)
    std::string indent = "  ";      ///< Default: 2 spaces
    bool alignment = true;

    // Behavior (override semantics)
    StrictnessMode strictness = StrictnessMode::Lenient;
    int verbosity = 1;
    bool single_unit = true;    ///< Default: treat all files as single compilation unit

    /// Convert to AutosToolOptions (for use with AutosTool)
    [[nodiscard]] struct AutosToolOptions toToolOptions() const;
};

/// Loads and merges configuration from multiple sources.
class ConfigLoader {
public:
    static constexpr const char* CONFIG_FILENAME = ".slang-autos.toml";

    /// Find the configuration file by searching:
    /// 1. Starting directory (typically CWD or file's directory)
    /// 2. Git repository root
    /// Returns the path if found, nullopt otherwise.
    [[nodiscard]] static std::optional<std::filesystem::path> findConfigFile(
        const std::filesystem::path& start_dir = std::filesystem::current_path());

    /// Find the git repository root by searching upward for .git directory.
    [[nodiscard]] static std::optional<std::filesystem::path> findGitRoot(
        const std::filesystem::path& start_dir);

    /// Load and parse a TOML configuration file.
    /// @param config_path Path to the .slang-autos.toml file
    /// @param diagnostics Optional collector for parse errors
    /// @return Parsed config or nullopt on error
    [[nodiscard]] static std::optional<FileConfig> loadFile(
        const std::filesystem::path& config_path,
        DiagnosticCollector* diagnostics = nullptr);

    /// Merge configurations with priority: CLI > inline > file > defaults.
    /// Library paths are additive; other options use override semantics.
    /// @param file_config Config from .slang-autos.toml (lowest priority)
    /// @param inline_config Config from file comments
    /// @param cli_options Options parsed from command line (highest priority)
    /// @param cli_flags Which CLI options were explicitly set
    [[nodiscard]] static MergedConfig merge(
        const std::optional<FileConfig>& file_config,
        const InlineConfig& inline_config,
        const struct AutosToolOptions& cli_options,
        const CliFlags& cli_flags = {});
};

} // namespace slang_autos
