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

/// Strip all AUTO expansion blocks from source text, leaving only markers.
/// This is used by --clean mode to remove stale expansions before re-running.
///
/// Handles:
/// - AUTOLOGIC: removes from "// Beginning of automatic logic" to "// End of automatics"
/// - AUTOPORTS: removes everything between /*AUTOPORTS*/ and the closing )
/// - AUTOINST: removes everything between /*AUTOINST*/ and the closing )
///
/// Returns the cleaned source text.
static std::string stripAutoExpansions(const std::string& source) {
    std::string result;
    result.reserve(source.size());

    size_t pos = 0;
    while (pos < source.size()) {
        // Look for markers
        size_t autoinst_pos = source.find("/*AUTOINST*/", pos);
        size_t autoports_pos = source.find("/*AUTOPORTS*/", pos);
        size_t begin_auto_pos = source.find("// Beginning of automatic", pos);

        // Find the nearest marker
        size_t next_marker = std::min({autoinst_pos, autoports_pos, begin_auto_pos});

        if (next_marker == std::string::npos) {
            // No more markers, copy rest of file
            result.append(source, pos, source.size() - pos);
            break;
        }

        // Copy everything up to the marker
        result.append(source, pos, next_marker - pos);

        if (next_marker == autoinst_pos) {
            // Handle AUTOINST: keep the marker, remove content until )
            result.append("/*AUTOINST*/");
            pos = autoinst_pos + 12;  // strlen("/*AUTOINST*/")

            // Find the closing ) - need to handle nested parens
            int paren_depth = 0;
            bool in_string = false;
            bool in_line_comment = false;
            bool in_block_comment = false;
            size_t close_pos = pos;

            while (close_pos < source.size()) {
                char c = source[close_pos];
                char next = (close_pos + 1 < source.size()) ? source[close_pos + 1] : '\0';

                if (in_line_comment) {
                    if (c == '\n') in_line_comment = false;
                } else if (in_block_comment) {
                    if (c == '*' && next == '/') {
                        in_block_comment = false;
                        close_pos++;
                    }
                } else if (in_string) {
                    if (c == '"' && (close_pos == 0 || source[close_pos-1] != '\\')) {
                        in_string = false;
                    }
                } else {
                    if (c == '/' && next == '/') {
                        in_line_comment = true;
                        close_pos++;
                    } else if (c == '/' && next == '*') {
                        in_block_comment = true;
                        close_pos++;
                    } else if (c == '"') {
                        in_string = true;
                    } else if (c == '(') {
                        paren_depth++;
                    } else if (c == ')') {
                        if (paren_depth == 0) {
                            // Found the closing paren
                            break;
                        }
                        paren_depth--;
                    }
                }
                close_pos++;
            }

            // Skip to the closing paren (but don't include it - it's part of the instance)
            pos = close_pos;

        } else if (next_marker == autoports_pos) {
            // Handle AUTOPORTS: keep the marker, remove content until )
            // AUTOPORTS is in module header: module foo ( ... /*AUTOPORTS*/ ... );
            result.append("/*AUTOPORTS*/");
            pos = autoports_pos + 13;  // strlen("/*AUTOPORTS*/")

            // Find the closing ) of the port list
            int paren_depth = 0;
            bool in_string = false;
            bool in_line_comment = false;
            bool in_block_comment = false;
            size_t close_pos = pos;

            while (close_pos < source.size()) {
                char c = source[close_pos];
                char next = (close_pos + 1 < source.size()) ? source[close_pos + 1] : '\0';

                if (in_line_comment) {
                    if (c == '\n') in_line_comment = false;
                } else if (in_block_comment) {
                    if (c == '*' && next == '/') {
                        in_block_comment = false;
                        close_pos++;
                    }
                } else if (in_string) {
                    if (c == '"' && (close_pos == 0 || source[close_pos-1] != '\\')) {
                        in_string = false;
                    }
                } else {
                    if (c == '/' && next == '/') {
                        in_line_comment = true;
                        close_pos++;
                    } else if (c == '/' && next == '*') {
                        in_block_comment = true;
                        close_pos++;
                    } else if (c == '"') {
                        in_string = true;
                    } else if (c == '(') {
                        paren_depth++;
                    } else if (c == ')') {
                        if (paren_depth == 0) {
                            // Found the closing paren of the port list
                            break;
                        }
                        paren_depth--;
                    }
                }
                close_pos++;
            }

            // Skip to the closing paren (but don't include it)
            pos = close_pos;

        } else {
            // Handle AUTOLOGIC block: remove from "// Beginning" to "// End of automatics"
            size_t end_pos = source.find("// End of automatics", next_marker);
            if (end_pos != std::string::npos) {
                // Skip past the end marker
                pos = end_pos + 20;  // strlen("// End of automatics")
            } else {
                // No end marker found - just copy the beginning marker and continue
                result.append(source, next_marker, 25);  // "// Beginning of automatic"
                pos = next_marker + 25;
            }
        }
    }

    return result;
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
    std::optional<bool> cleanMode;
    driver.cmdLine.add("--dry-run", dryRun, "Show changes without modifying files");
    driver.cmdLine.add("--diff", diffMode, "Output unified diff instead of modifying");
    driver.cmdLine.add("--check", checkMode, "Check if files need changes (exit 1 if changes needed, for CI)");
    driver.cmdLine.add("--clean", cleanMode, "Remove all AUTO expansion blocks, leaving only markers");

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

    // Suppress include file errors at the slang level.
    // We only care about include errors in the top module and direct children.
    // Grandchildren with missing includes should not block expansion.
    // We'll check for critical include errors per-file in the expansion loop.
    driver.diagEngine.setSeverity(slang::diag::CouldNotOpenIncludeFile,
                                  slang::DiagnosticSeverity::Warning);

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

    // Scan files to expand for inline configs
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
    // Clean mode: strip AUTO expansions and exit (no slang compilation needed)
    // ========================================================================

    if (cleanMode.value_or(false)) {
        int files_cleaned = 0;

        for (const auto& path : filesToExpand) {
            std::ifstream ifs(path);
            if (!ifs) {
                OS::printE(fmt::format("error: Failed to open file: {}\n", path.string()));
                continue;
            }

            std::stringstream buffer;
            buffer << ifs.rdbuf();
            std::string original = buffer.str();
            ifs.close();

            std::string cleaned = stripAutoExpansions(original);

            if (cleaned != original) {
                if (dryRun.value_or(false) || diffMode.value_or(false)) {
                    if (diffMode.value_or(false)) {
                        // Generate diff output
                        OS::print(fmt::format("--- {}\n+++ {}\n", path.string(), path.string()));
                        // Simple diff: show that content changed
                        OS::print("@@ cleaned AUTO expansion blocks @@\n");
                    }
                    OS::print(fmt::format("Would clean: {}\n", path.string()));
                } else {
                    std::ofstream ofs(path);
                    if (!ofs) {
                        OS::printE(fmt::format("error: Failed to write file: {}\n", path.string()));
                        continue;
                    }
                    ofs << cleaned;
                    ofs.close();
                    ++files_cleaned;
                    if (verbosity >= 1) {
                        OS::print(fmt::format("Cleaned: {}\n", path.string()));
                    }
                }
            } else {
                if (verbosity >= 2) {
                    OS::print(fmt::format("No expansions to clean: {}\n", path.string()));
                }
            }
        }

        if (verbosity >= 1 && !dryRun.value_or(false)) {
            OS::print(fmt::format("Cleaned {} file(s)\n", files_cleaned));
        }

        return 0;
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
    int total_autoports = 0;
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
        // Critical errors that prevent correct expansion:
        // - InvalidTopModule: module name doesn't match filename
        // - CouldNotOpenIncludeFile: missing include file (macros won't be defined)
        // - UnknownDirective: undefined macro (will cause garbage output)
        //
        // Since we only add top + direct children to slang (not grandchildren),
        // these errors will only occur in files we care about.
        {
            auto& diags = compilation->getAllDiagnostics();
            bool hasInvalidTop = false;
            bool hasCriticalError = false;
            std::vector<std::string> criticalMessages;

            // Get canonical path for comparison
            fs::path canonical_top = fs::canonical(path);

            for (const auto& d : diags) {
                if (d.code == slang::diag::InvalidTopModule) {
                    hasInvalidTop = true;
                    hasCriticalError = true;
                }
                // Check for missing include files - only critical if in top file
                // Grandchildren with missing includes should not block expansion
                else if (d.code == slang::diag::CouldNotOpenIncludeFile) {
                    auto error_file = driver.sourceManager.getFileName(d.location);
                    bool in_top = false;
                    if (!error_file.empty()) {
                        try {
                            in_top = (fs::canonical(std::string(error_file)) == canonical_top);
                        } catch (...) {}
                    }
                    if (in_top) {
                        hasCriticalError = true;
                        criticalMessages.push_back("Missing include file - macros may be undefined");
                    }
                }
                // Check for unknown directives (undefined macros) - only critical if in top file
                else if (d.code == slang::diag::UnknownDirective) {
                    auto error_file = driver.sourceManager.getFileName(d.location);
                    bool in_top = false;
                    if (!error_file.empty()) {
                        try {
                            in_top = (fs::canonical(std::string(error_file)) == canonical_top);
                        } catch (...) {}
                    }
                    if (in_top) {
                        hasCriticalError = true;
                        criticalMessages.push_back("Undefined macro or directive");
                    }
                }
            }

            // Always show critical slang diagnostics, verbose mode shows all
            if (hasCriticalError || (verbosity >= 2 && !diags.empty())) {
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

            // Block expansion on critical preprocessing errors
            // These will cause garbage output if we proceed
            if (hasCriticalError) {
                any_errors = true;
                OS::printE(fmt::format(
                    "error: Cannot expand '{}' due to preprocessing errors.\n"
                    "       Check that all include directories are specified with -I or +incdir+\n"
                    "       and that all required macros are defined with +define+.\n",
                    path.string()));
                continue;
            }

            // For other slang errors (timescale, elaboration, etc.): proceed with expansion
            // These typically don't affect port parsing
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
        total_autoports += result.autoports_count;

        if (result.hasChanges()) {
            ++files_changed;

            if (diff_mode) {
                SourceWriter writer(true);
                OS::print(writer.generateDiff(path, result.original_content,
                                              result.modified_content));
            } else if (verbosity >= 1) {
                OS::print(fmt::format("{}: {} AUTOINST, {} AUTOLOGIC, {} AUTOPORTS\n",
                                      path.string(), result.autoinst_count,
                                      result.autologic_count, result.autoports_count));
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
        OS::print(fmt::format("\nSummary: {} file(s) {}changed, {} AUTOINST, {} AUTOLOGIC, {} AUTOPORTS\n",
                              files_changed, change_verb,
                              total_autoinst, total_autologic, total_autoports));
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
