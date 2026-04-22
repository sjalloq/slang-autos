#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/diagnostics/AllDiags.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/util/VersionInfo.h"

#include "slang-autos/DotStarExpander.h"
#include "slang-autos/Writer.h"

using namespace slang;
using namespace slang::driver;
using namespace slang_autos;

namespace fs = std::filesystem;

static bool isValidExtension(const fs::path& path) {
    auto ext = path.extension().string();
    return ext == ".v" || ext == ".sv" || ext == ".vh" || ext == ".svh";
}

int main(int argc, char* argv[]) {
    Driver driver;
    driver.addStandardArgs();

    // Identify positional files for expansion (before slang parses argv)
    std::vector<fs::path> filesToExpand;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg.starts_with('-') || arg.starts_with('+')) {
            continue;
        }
        fs::path path(arg);
        if (fs::exists(path) && isValidExtension(path)) {
            filesToExpand.push_back(path);
        }
    }

    // CLI options
    std::optional<bool> showHelp;
    std::optional<bool> showVersion;
    driver.cmdLine.add("-h,--help", showHelp, "Display available options");
    driver.cmdLine.add("--version", showVersion, "Display version information and exit");

    std::optional<bool> dryRun;
    std::optional<bool> diffMode;
    std::optional<bool> checkMode;
    driver.cmdLine.add("--dry-run", dryRun, "Show changes without modifying files");
    driver.cmdLine.add("--diff", diffMode, "Output unified diff instead of modifying");
    driver.cmdLine.add("--check", checkMode, "Check if files need changes (exit 1 if changes needed)");

    std::optional<bool> strictMode;
    driver.cmdLine.add("--strict", strictMode, "Error on missing modules (default: warn and continue)");

    std::optional<bool> noAlignment;
    driver.cmdLine.add("--no-alignment", noAlignment, "Don't align port names");

    std::optional<bool> verbose;
    std::optional<bool> quiet;
    driver.cmdLine.add("--verbose", verbose, "Increase verbosity");
    driver.cmdLine.add("-q,--quiet", quiet, "Suppress non-error output");

    // Parse command line
    if (!driver.parseCommandLine(argc, argv))
        return 1;

    if (showHelp == true) {
        OS::print(driver.cmdLine.getHelpText(
            "slang-expand - Expand SystemVerilog .* port wildcards"));
        return 0;
    }

    if (showVersion == true) {
        OS::print(fmt::format("slang-expand version 0.1.0 (slang {}.{}.{}+{})\n",
                              VersionInfo::getMajor(), VersionInfo::getMinor(),
                              VersionInfo::getPatch(), std::string(VersionInfo::getHash())));
        return 0;
    }

    if (!driver.processOptions())
        return 2;

    // Always ignore unknown modules
    driver.options.compilationFlags[ast::CompilationFlags::IgnoreUnknownModules] = true;

    // Suppress include file errors at the slang level
    driver.diagEngine.setSeverity(slang::diag::CouldNotOpenIncludeFile,
                                  slang::DiagnosticSeverity::Warning);

    int verbosity = quiet.value_or(false) ? 0 : (verbose.value_or(false) ? 2 : 1);

    if (filesToExpand.empty()) {
        OS::printE("error: no input files specified\n");
        OS::printE("Run with --help for usage information\n");
        return 1;
    }

    // Parse all sources
    if (!driver.parseAllSources())
        return 3;

    // Expansion options
    DotStarExpanderOptions expand_opts;
    expand_opts.alignment = !noAlignment.value_or(false);
    expand_opts.strictness = strictMode.value_or(false) ? StrictnessMode::Strict
                                                        : StrictnessMode::Lenient;
    expand_opts.verbosity = verbosity;

    bool dry_run = dryRun.value_or(false);
    bool diff_mode = diffMode.value_or(false);
    bool check_mode = checkMode.value_or(false);
    bool no_write = dry_run || diff_mode || check_mode;

    int total_expanded = 0;
    int files_changed = 0;
    bool any_errors = false;

    for (const auto& path : filesToExpand) {
        if (verbosity >= 2) {
            OS::print(fmt::format("Processing: {}\n", path.string()));
        }

        // Set --top to the filename stem
        driver.options.topModules = {path.stem().string()};

        auto compilation = driver.createCompilation();

        // Check for critical slang diagnostics
        {
            auto& diags = compilation->getAllDiagnostics();
            bool hasInvalidTop = false;
            bool hasCriticalError = false;

            fs::path canonical_top = fs::canonical(path);

            for (const auto& d : diags) {
                if (d.code == slang::diag::InvalidTopModule) {
                    hasInvalidTop = true;
                    hasCriticalError = true;
                } else if (d.code == slang::diag::CouldNotOpenIncludeFile ||
                           d.code == slang::diag::UnknownDirective) {
                    auto error_file = driver.sourceManager.getFileName(d.location);
                    bool in_top = false;
                    if (!error_file.empty()) {
                        try {
                            in_top = (fs::canonical(std::string(error_file)) == canonical_top);
                        } catch (...) {}
                    }
                    if (in_top) {
                        hasCriticalError = true;
                    }
                }
            }

            if (hasCriticalError || (verbosity >= 2 && !diags.empty())) {
                OS::printE(slang::DiagnosticEngine::reportAll(
                    driver.sourceManager, diags));
            }

            if (hasInvalidTop) {
                any_errors = true;
                OS::printE(fmt::format(
                    "note: module name must match filename. "
                    "Expected module '{}' in file '{}'.\n",
                    path.stem().string(), path.string()));
                continue;
            }

            if (hasCriticalError) {
                any_errors = true;
                OS::printE(fmt::format(
                    "error: Cannot expand '{}' due to preprocessing errors.\n",
                    path.string()));
                continue;
            }
        }

        // Read source file
        std::ifstream ifs(path);
        if (!ifs) {
            OS::printE(fmt::format("error: Failed to open file: {}\n", path.string()));
            any_errors = true;
            continue;
        }
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        std::string original_content = buffer.str();
        ifs.close();

        // Parse syntax tree from original source
        auto tree = slang::syntax::SyntaxTree::fromText(original_content);
        if (!tree) {
            OS::printE(fmt::format("error: Failed to parse: {}\n", path.string()));
            any_errors = true;
            continue;
        }

        // Expand dot-star wildcards
        DotStarExpander expander(*compilation, expand_opts);
        expander.analyze(tree, original_content);

        auto& repls = expander.getReplacements();

        std::string modified_content;
        if (repls.empty()) {
            modified_content = original_content;
        } else {
            SourceWriter writer(false);
            modified_content = writer.applyReplacements(original_content, repls);
        }

        bool changed = (original_content != modified_content);
        int count = expander.expandedCount();
        total_expanded += count;

        if (changed) {
            ++files_changed;

            if (diff_mode) {
                SourceWriter writer(true);
                OS::print(writer.generateDiff(path, original_content, modified_content));
            } else if (verbosity >= 1) {
                OS::print(fmt::format("{}: expanded {} .* wildcard(s)\n",
                                      path.string(), count));
            }

            if (!no_write) {
                SourceWriter writer(false);
                writer.writeFile(path, modified_content);
            }
        } else if (verbosity >= 2) {
            OS::print(fmt::format("{}: no .* wildcards found\n", path.string()));
        }

        // Report expander diagnostics
        if (expander.diagnostics().hasErrors()) {
            any_errors = true;
            OS::printE(expander.diagnostics().format());
        } else if (expander.diagnostics().warningCount() > 0 && verbosity >= 1) {
            OS::printE(expander.diagnostics().format());
        }
    }

    // Summary
    if (verbosity >= 1 && !diff_mode) {
        std::string change_verb = no_write ? "would be " : "";
        OS::print(fmt::format("\nSummary: {} file(s) {}changed, {} .* wildcard(s) expanded\n",
                              files_changed, change_verb, total_expanded));
    }

    if (check_mode && files_changed > 0) {
        if (verbosity >= 1) {
            OS::printE("error: files contain unexpanded .* wildcards "
                       "(run without --check to apply)\n");
        }
        return 1;
    }

    return any_errors ? 1 : 0;
}
