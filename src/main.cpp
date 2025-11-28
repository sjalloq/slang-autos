#include <iostream>
#include <filesystem>

#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/util/VersionInfo.h"

#include "slang-autos/Tool.h"
#include "slang-autos/Writer.h"

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
    driver.cmdLine.add("--dry-run", dryRun, "Show changes without modifying files");
    driver.cmdLine.add("--diff", diffMode, "Output unified diff instead of modifying");

    // Strictness
    std::optional<bool> strictMode;
    driver.cmdLine.add("--strict", strictMode,
                       "Error on missing modules (default: warn and continue)");

    // Formatting options
    std::optional<uint32_t> indentSpaces;
    driver.cmdLine.add("--indent", indentSpaces,
                       "Indentation width in spaces (default: 4)", "<width>");

    std::optional<bool> noAlignment;
    driver.cmdLine.add("--no-alignment", noAlignment, "Don't align port names");

    // Verbosity (note: -v is used by slang for library files, so we only use long form)
    std::optional<bool> verbose;
    std::optional<bool> quiet;
    driver.cmdLine.add("--verbose", verbose, "Increase verbosity");
    driver.cmdLine.add("-q,--quiet", quiet, "Suppress non-error output");

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
    // Build tool options
    // ========================================================================

    int verbosity = quiet.value_or(false) ? 0 : (verbose.value_or(false) ? 2 : 1);

    AutosTool::Options options;
    options.strictness = strictMode.value_or(false) ? StrictnessMode::Strict
                                                    : StrictnessMode::Lenient;
    options.alignment = !noAlignment.value_or(false);
    options.indent = std::string(indentSpaces.value_or(4), ' ');
    options.verbosity = verbosity;

    // ========================================================================
    // Check for files to expand
    // ========================================================================

    if (filesToExpand.empty()) {
        OS::printE("error: no input files specified\n");
        OS::printE("Run with --help for usage information\n");
        return 1;
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

    int total_autoinst = 0;
    int total_autowire = 0;
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

        // Expand autos in this file
        AutosTool tool(options);
        tool.setCompilation(std::move(compilation));

        auto result = tool.expandFile(path, dry_run || diff_mode);

        if (!result.success) {
            any_errors = true;
            // Print diagnostics for this file
            if (tool.diagnostics().hasErrors() ||
                (verbosity >= 1 && tool.diagnostics().warningCount() > 0)) {
                OS::printE(tool.diagnostics().format());
            }
            continue;
        }

        total_autoinst += result.autoinst_count;
        total_autowire += result.autowire_count;

        if (result.hasChanges()) {
            ++files_changed;

            if (diff_mode) {
                SourceWriter writer(true);
                OS::print(writer.generateDiff(path, result.original_content,
                                              result.modified_content));
            } else if (verbosity >= 1) {
                OS::print(fmt::format("{}: {} AUTOINST, {} AUTOWIRE\n",
                                      path.string(), result.autoinst_count,
                                      result.autowire_count));
            }
        }

        // Print diagnostics for this file (warnings even on success)
        if (verbosity >= 1 && tool.diagnostics().warningCount() > 0) {
            OS::printE(tool.diagnostics().format());
        }
    }

    // Print summary
    if (verbosity >= 1 && !diff_mode) {
        OS::print(fmt::format("\nSummary: {} file(s) {}changed, {} AUTOINST, {} AUTOWIRE\n",
                              files_changed, dry_run ? "would be " : "",
                              total_autoinst, total_autowire));
    }

    return any_errors ? 1 : 0;
}
