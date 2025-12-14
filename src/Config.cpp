#include "slang-autos/Config.h"
#include "slang-autos/Parser.h"
#include "slang-autos/Tool.h"

#include <toml++/toml.hpp>

#include <fstream>

namespace slang_autos {

// ============================================================================
// MergedConfig implementation
// ============================================================================

AutosToolOptions MergedConfig::toToolOptions() const {
    AutosToolOptions opts;
    opts.strictness = strictness;
    opts.alignment = alignment;
    opts.indent = indent;
    opts.verbosity = verbosity;
    opts.resolved_ranges = resolved_ranges;
    return opts;
}

// ============================================================================
// ConfigLoader implementation
// ============================================================================

std::optional<std::filesystem::path> ConfigLoader::findGitRoot(
    const std::filesystem::path& start_dir) {

    namespace fs = std::filesystem;

    fs::path current = fs::absolute(start_dir);

    // Walk up the directory tree looking for .git
    while (!current.empty() && current.has_parent_path()) {
        fs::path git_dir = current / ".git";
        if (fs::exists(git_dir)) {
            return current;
        }

        fs::path parent = current.parent_path();
        if (parent == current) {
            break;  // Reached filesystem root
        }
        current = parent;
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> ConfigLoader::findConfigFile(
    const std::filesystem::path& start_dir) {

    namespace fs = std::filesystem;

    // 1. Check starting directory
    fs::path config_in_start = start_dir / CONFIG_FILENAME;
    if (fs::exists(config_in_start)) {
        return config_in_start;
    }

    // 2. Check git repository root
    if (auto git_root = findGitRoot(start_dir)) {
        fs::path config_in_git = *git_root / CONFIG_FILENAME;
        if (fs::exists(config_in_git)) {
            return config_in_git;
        }
    }

    return std::nullopt;
}

std::optional<FileConfig> ConfigLoader::loadFile(
    const std::filesystem::path& config_path,
    DiagnosticCollector* diagnostics) {

    FileConfig config;

    try {
        toml::table tbl = toml::parse_file(config_path.string());

        // [library] section
        if (auto library = tbl["library"].as_table()) {
            // libdirs
            if (auto arr = (*library)["libdirs"].as_array()) {
                std::vector<std::string> dirs;
                for (const auto& elem : *arr) {
                    if (auto str = elem.as_string()) {
                        dirs.push_back(std::string(str->get()));
                    }
                }
                if (!dirs.empty()) {
                    config.libdirs = std::move(dirs);
                }
            }

            // libext
            if (auto arr = (*library)["libext"].as_array()) {
                std::vector<std::string> exts;
                for (const auto& elem : *arr) {
                    if (auto str = elem.as_string()) {
                        exts.push_back(std::string(str->get()));
                    }
                }
                if (!exts.empty()) {
                    config.libext = std::move(exts);
                }
            }

            // incdirs
            if (auto arr = (*library)["incdirs"].as_array()) {
                std::vector<std::string> dirs;
                for (const auto& elem : *arr) {
                    if (auto str = elem.as_string()) {
                        dirs.push_back(std::string(str->get()));
                    }
                }
                if (!dirs.empty()) {
                    config.incdirs = std::move(dirs);
                }
            }
        }

        // [formatting] section
        if (auto formatting = tbl["formatting"].as_table()) {
            // indent
            if (auto val = (*formatting)["indent"].as_integer()) {
                config.indent = static_cast<int>(val->get());
            } else if (auto str = (*formatting)["indent"].as_string()) {
                if (str->get() == "tab") {
                    config.indent = -1;  // Special value for tab
                }
            }

            // alignment
            if (auto val = (*formatting)["alignment"].as_boolean()) {
                config.alignment = val->get();
            }
        }

        // [behavior] section
        if (auto behavior = tbl["behavior"].as_table()) {
            // strictness
            if (auto str = (*behavior)["strictness"].as_string()) {
                std::string_view val = str->get();
                if (val == "strict") {
                    config.strictness = StrictnessMode::Strict;
                } else if (val == "lenient") {
                    config.strictness = StrictnessMode::Lenient;
                } else if (diagnostics) {
                    diagnostics->addWarning(
                        "Unknown strictness value: " + std::string(val) +
                        " (expected 'strict' or 'lenient')",
                        config_path.string(), 0, "config");
                }
            }

            // verbosity
            if (auto val = (*behavior)["verbosity"].as_integer()) {
                config.verbosity = static_cast<int>(val->get());
            }

            // single_unit
            if (auto val = (*behavior)["single_unit"].as_boolean()) {
                config.single_unit = val->get();
            }

            // resolved_ranges
            if (auto val = (*behavior)["resolved_ranges"].as_boolean()) {
                config.resolved_ranges = val->get();
            }
        }

        return config;

    } catch (const toml::parse_error& err) {
        if (diagnostics) {
            diagnostics->addError(
                std::string("Failed to parse config file: ") + err.what(),
                config_path.string(),
                err.source().begin.line,
                "config");
        }
        return std::nullopt;
    }
}

MergedConfig ConfigLoader::merge(
    const std::optional<FileConfig>& file_config,
    const InlineConfig& inline_config,
    const AutosToolOptions& cli_options,
    const CliFlags& cli_flags) {

    MergedConfig result;

    // Start with defaults (already set in MergedConfig struct)

    // Layer 1: File config (lowest priority for non-additive options)
    if (file_config) {
        // Additive: library paths
        if (file_config->libdirs) {
            for (const auto& dir : *file_config->libdirs) {
                result.libdirs.push_back(dir);
            }
        }
        if (file_config->libext) {
            for (const auto& ext : *file_config->libext) {
                result.libext.push_back(ext);
            }
        }
        if (file_config->incdirs) {
            for (const auto& dir : *file_config->incdirs) {
                result.incdirs.push_back(dir);
            }
        }

        // Override: formatting/behavior
        if (file_config->indent) {
            int spaces = *file_config->indent;
            if (spaces == -1) {
                result.indent = "\t";
            } else {
                result.indent = std::string(static_cast<size_t>(spaces), ' ');
            }
        }
        if (file_config->alignment) {
            result.alignment = *file_config->alignment;
        }
        if (file_config->strictness) {
            result.strictness = *file_config->strictness;
        }
        if (file_config->verbosity) {
            result.verbosity = *file_config->verbosity;
        }
        if (file_config->single_unit) {
            result.single_unit = *file_config->single_unit;
        }
        if (file_config->resolved_ranges) {
            result.resolved_ranges = *file_config->resolved_ranges;
        }
    }

    // Layer 2: Inline config (overrides file config)
    // Additive: library paths
    for (const auto& dir : inline_config.libdirs) {
        result.libdirs.push_back(dir);
    }
    for (const auto& ext : inline_config.libext) {
        result.libext.push_back(ext);
    }
    for (const auto& dir : inline_config.incdirs) {
        result.incdirs.push_back(dir);
    }

    // Override: formatting/behavior from inline config
    if (inline_config.indent) {
        int spaces = *inline_config.indent;
        if (spaces == -1) {
            result.indent = "\t";
        } else {
            result.indent = std::string(static_cast<size_t>(spaces), ' ');
        }
    }
    if (inline_config.alignment) {
        result.alignment = *inline_config.alignment;
    }
    if (inline_config.strictness) {
        result.strictness = *inline_config.strictness;
    }
    if (inline_config.resolved_ranges) {
        result.resolved_ranges = *inline_config.resolved_ranges;
    }

    // Layer 3: CLI options (highest priority)
    // CLI overrides all if explicitly specified
    if (cli_flags.has_indent) {
        result.indent = cli_options.indent;
    }
    if (cli_flags.has_alignment) {
        result.alignment = cli_options.alignment;
    }
    if (cli_flags.has_strictness) {
        result.strictness = cli_options.strictness;
    }
    if (cli_flags.has_verbosity) {
        result.verbosity = cli_options.verbosity;
    }
    if (cli_flags.has_single_unit) {
        result.single_unit = cli_options.single_unit;
    }
    if (cli_flags.has_resolved_ranges) {
        result.resolved_ranges = cli_options.resolved_ranges;
    }

    return result;
}

} // namespace slang_autos
