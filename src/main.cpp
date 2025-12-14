#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/diagnostics/AllDiags.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/util/VersionInfo.h"

#include "slang-autos/Tool.h"
#include "slang-autos/Writer.h"
#include "slang-autos/Config.h"
#include "slang-autos/Parser.h"
#include "slang-autos/Diagnostics.h"

using namespace slang;
using namespace slang::driver;
using namespace slang_autos;

namespace fs = std::filesystem;

// Valid Verilog/SystemVerilog extensions
static bool isValidExtension(const fs::path& path) {
    auto ext = path.extension().string();
    return ext == ".v" || ext == ".sv" || ext == ".vh" || ext == ".svh";
}

int main(int argc, char* argv[]) {
    Driver driver;
    driver.addStandardArgs();

    // ========================================================================
    // Identify positional files for expansion (before slang parses argv)
    // ========================================================================
    // We scan argv to find files that will be expanded. These are:
    // - Not options (don't start with '-')
    // - Not EDA-style options (don't start with '+')
    // - Have valid Verilog/SystemVerilog extensions
    // - Actually exist on the filesystem
    //
    // This doesn't duplicate slang's parsing - we're just identifying which
    // of the positional arguments are files we should expand.
    // ========================================================================

    std::vector<fs::path> filesToExpand;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        // Skip options (start with '-' or '+')
        if (arg.starts_with('-') || arg.starts_with('+')) {
            continue;
        }

        // Check if this is a valid Verilog/SystemVerilog file to expand
        fs::path path(arg);

        // Must exist and have valid extension to be expanded
        // (other positional args like module names for --top are left for slang)
        if (fs::exists(path) && isValidExtension(path)) {
            filesToExpand.push_back(path);
        }
    }

    // ========================================================================
    // slang-autos specific options (added to slang's command line parser)
    // ========================================================================

    std::optional<bool> showHelp;
    std::optional<bool> showVersion;
    driver.cmdLine.add("-h,--help", showHelp, "Display available options");
    driver.cmdLine.add("--version", showVersion, "Display version information and exit");

    // Output modes
    std::optional<bool> dryRun;
    std::optional<bool> diffMode;
    std::optional<bool> checkMode;
    driver.cmdLine.add("--dry-run", dryRun, "Show changes without modifying files");
    driver.cmdLine.add("--diff", diffMode, "Output unified diff instead of modifying");
    driver.cmdLine.add("--check", checkMode, "Check if files need changes (exit 1 if changes needed, for CI)");

    // Strictness
    std::optional<bool> strictMode;
    driver.cmdLine.add("--strict", strictMode,
                       "Error on missing modules (default: warn and continue)");

    // Formatting options
    std::optional<bool> noAlignment;
    driver.cmdLine.add("--no-alignment", noAlignment, "Don't align port names");

    // Verbosity (note: -v is used by slang for library files, so we only use long form)
    std::optional<bool> verbose;
    std::optional<bool> quiet;
    driver.cmdLine.add("--verbose", verbose, "Increase verbosity");
    driver.cmdLine.add("-q,--quiet", quiet, "Suppress non-error output");

    // Compilation unit mode (default: single unit for better macro handling)
    std::optional<bool> noSingleUnit;
    driver.cmdLine.add("--no-single-unit", noSingleUnit,
                       "Treat files as separate compilation units (disables default --single-unit)");

    // Output format options
    std::optional<bool> resolvedRanges;
    driver.cmdLine.add("--resolved-ranges", resolvedRanges,
                       "Use resolved integer widths instead of original parameter/expression syntax");

    // ========================================================================
    // Parse command line
    // ========================================================================

    if (!driver.parseCommandLine(argc, argv))
        return 1;

    if (showHelp == true) {
        OS::print(driver.cmdLine.getHelpText("slang-autos - SystemVerilog AUTO macro expander"));
        return 0;
    }

    if (showVersion == true) {
        OS::print(fmt::format("slang-autos version 0.1.0 (slang {}.{}.{}+{})\n",
                              VersionInfo::getMajor(), VersionInfo::getMinor(),
                              VersionInfo::getPatch(), std::string(VersionInfo::getHash())));
        return 0;
    }

    if (!driver.processOptions())
        return 2;

    // Always ignore unknown modules - we don't need leaf cells elaborated
    driver.options.compilationFlags[ast::CompilationFlags::IgnoreUnknownModules] = true;

    // ========================================================================
    // Load configuration file (.slang-autos.toml)
    // ========================================================================

    std::optional<FileConfig> file_config;
    if (auto config_path = ConfigLoader::findConfigFile()) {
        file_config = ConfigLoader::loadFile(*config_path);
        // Note: Library paths from config file should ideally be added to
        // driver.options before parseAllSources(). For now, we only use
        // the formatting/behavior options.
    }

    // ========================================================================
    // Build tool options (merging CLI > config file > defaults)
    // ========================================================================

    // Track which CLI options were explicitly specified
    CliFlags cli_flags;
    cli_flags.has_strictness = strictMode.has_value();
    cli_flags.has_alignment = noAlignment.has_value();
    cli_flags.has_indent = false;  // No CLI --indent option anymore
    cli_flags.has_verbosity = verbose.has_value() || quiet.has_value();
    cli_flags.has_single_unit = noSingleUnit.has_value();
    cli_flags.has_resolved_ranges = resolvedRanges.has_value();

    // Build CLI options (these are the "raw" CLI values)
    AutosTool::Options cli_options;
    cli_options.strictness = strictMode.value_or(false) ? StrictnessMode::Strict
                                                        : StrictnessMode::Lenient;
    cli_options.alignment = !noAlignment.value_or(false);
    cli_options.indent = "  ";  // Default 2 spaces (can be overridden by file config or inline)
    cli_options.verbosity = quiet.value_or(false) ? 0 : (verbose.value_or(false) ? 2 : 1);
    cli_options.single_unit = !noSingleUnit.value_or(false);
    cli_options.resolved_ranges = resolvedRanges.value_or(false);

    // Merge: CLI > config file > defaults
    // Note: Inline config is handled per-file in the expansion loop
    InlineConfig empty_inline;  // Will be merged per-file
    MergedConfig merged = ConfigLoader::merge(file_config, empty_inline, cli_options, cli_flags);
    AutosTool::Options options = merged.toToolOptions();
    int verbosity = options.verbosity;

    // Apply single_unit setting to slang driver (must be before parseAllSources)
    // This makes macros from includes visible across all files
    driver.options.singleUnit = merged.single_unit;

    // ========================================================================
    // Check for files to expand
    // ========================================================================

    if (filesToExpand.empty()) {
        OS::printE("error: no input files specified\n");
        OS::printE("Run with --help for usage information\n");
        return 1;
    }

    // ========================================================================
    // Pre-scan files for inline config (before slang parses)
    // ========================================================================
    // We need to extract library paths from inline config BEFORE slang parses,
    // otherwise -y, +libext+, +incdir+ directives would be too late.
    // We also store the full inline config per-file to avoid re-parsing later.

    DiagnosticCollector prescan_diagnostics;
    std::unordered_map<std::string, InlineConfig> inline_configs;

    for (const auto& path : filesToExpand) {
        std::ifstream file(path);
        if (!file) continue;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        InlineConfig inline_cfg = parseInlineConfig(content, path.string(), &prescan_diagnostics);

        // Store for later use (avoids re-parsing in Tool::expandFile)
        inline_configs[path.string()] = inline_cfg;

        // Resolve paths relative to the source file's directory
        fs::path file_dir = fs::absolute(path).parent_path();

        // Add library search directories (-y equivalent)
        for (const auto& dir : inline_cfg.libdirs) {
            fs::path resolved = (file_dir / dir).lexically_normal();
            driver.sourceLoader.addSearchDirectories(resolved.string());
        }
        // Add library file extensions (+libext+ equivalent)
        for (const auto& ext : inline_cfg.libext) {
            driver.sourceLoader.addSearchExtension(ext);
        }
        // Add include directories (+incdir+ equivalent)
        for (const auto& dir : inline_cfg.incdirs) {
            fs::path resolved = (file_dir / dir).lexically_normal();
            driver.sourceManager.addUserDirectories(resolved.string());
        }
    }

    // Report any inline config diagnostics (always shown - these are config issues)
    for (const auto& diag : prescan_diagnostics.diagnostics()) {
        OS::printE(fmt::format("{}: {}{}\n",
            diag.level == DiagnosticLevel::Warning ? "warning" : "error",
            diag.message,
            diag.file_path.empty() ? "" : " [" + diag.file_path + "]"));
    }

    // ========================================================================
    // Parse all sources (syntax trees are reused across compilations)
    // ========================================================================

    if (!driver.parseAllSources())
        return 3;

    // ========================================================================
    // Run AUTO expansion (per-file compilation with --top set to filename)
    // ========================================================================

    bool dry_run = dryRun.value_or(false);
    bool diff_mode = diffMode.value_or(false);
    bool check_mode = checkMode.value_or(false);

    int total_autoinst = 0;
    int total_autologic = 0;
    int files_changed = 0;
    bool any_errors = false;

    for (const auto& path : filesToExpand) {
        if (verbosity >= 2) {
            OS::print(fmt::format("Processing: {}\n", path.string()));
        }

        // Set --top to the filename (e.g., "foo.sv" -> "foo")
        // This limits elaboration scope to just this module
        driver.options.topModules = {path.stem().string()};

        // Create compilation with this top module (reuses parsed syntax trees)
        auto compilation = driver.createCompilation();

        // Process slang diagnostics
        {
            auto& diags = compilation->getAllDiagnostics();
            bool hasSlangErrors = false;
            bool hasInvalidTop = false;

            for (const auto& d : diags) {
                if (d.code == slang::diag::InvalidTopModule) {
                    hasInvalidTop = true;
                }
                // Check if this is an error-level diagnostic
                auto severity = slang::getDefaultSeverity(d.code);
                if (severity == slang::DiagnosticSeverity::Error ||
                    severity == slang::DiagnosticSeverity::Fatal) {
                    hasSlangErrors = true;
                }
            }

            // In verbose mode, show all slang diagnostics
            if (verbosity >= 2 && !diags.empty()) {
                OS::printE(slang::DiagnosticEngine::reportAll(
                    driver.sourceManager, diags));
            }
            // Otherwise, only show if there are errors
            else if (hasSlangErrors && !diags.empty()) {
                OS::printE(slang::DiagnosticEngine::reportAll(
                    driver.sourceManager, diags));
            }

            // Special handling for InvalidTopModule
            if (hasInvalidTop) {
                any_errors = true;
                OS::printE(fmt::format(
                    "note: slang-autos requires the module name to match the filename.\n"
                    "      Expected module '{}' in file '{}'.\n",
                    path.stem().string(), path.string()));
                continue;
            }

            // Skip this file if slang found errors (but not just warnings)
            if (hasSlangErrors) {
                any_errors = true;
                continue;
            }
        }

        // Expand autos in this file
        AutosTool tool(options);
        tool.setCompilation(std::move(compilation));

        // Pass pre-parsed inline config (avoids re-parsing)
        auto it = inline_configs.find(path.string());
        if (it != inline_configs.end()) {
            tool.setInlineConfig(path, it->second);
        }

        auto result = tool.expandFile(path, dry_run || diff_mode || check_mode);

        if (!result.success) {
            any_errors = true;
            // Print diagnostics for this file (always show - these are config/tool issues)
            if (tool.diagnostics().hasErrors() || tool.diagnostics().warningCount() > 0) {
                OS::printE(tool.diagnostics().format());
            }
            continue;
        }

        total_autoinst += result.autoinst_count;
        total_autologic += result.autologic_count;

        if (result.hasChanges()) {
            ++files_changed;

            if (diff_mode) {
                SourceWriter writer(true);
                OS::print(writer.generateDiff(path, result.original_content,
                                              result.modified_content));
            } else if (verbosity >= 1) {
                OS::print(fmt::format("{}: {} AUTOINST, {} AUTOLOGIC\n",
                                      path.string(), result.autoinst_count,
                                      result.autologic_count));
            }
        }

        // Print diagnostics for this file (always show - these are config/tool issues)
        if (tool.diagnostics().hasErrors()) {
            any_errors = true;
            OS::printE(tool.diagnostics().format());
        } else if (tool.diagnostics().warningCount() > 0) {
            OS::printE(tool.diagnostics().format());
        }
    }

    // Print summary
    if (verbosity >= 1 && !diff_mode) {
        std::string change_verb = (dry_run || check_mode) ? "would be " : "";
        OS::print(fmt::format("\nSummary: {} file(s) {}changed, {} AUTOINST, {} AUTOLOGIC\n",
                              files_changed, change_verb,
                              total_autoinst, total_autologic));
    }

    // In check mode, exit 1 if any files would be changed (for CI)
    if (check_mode && files_changed > 0) {
        if (verbosity >= 1) {
            OS::printE("error: files need AUTO expansion (run without --check to apply)\n");
        }
        return 1;
    }

    return any_errors ? 1 : 0;
}
