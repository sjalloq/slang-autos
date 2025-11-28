#include <iostream>
#include <filesystem>
#include <vector>
#include <string>

#include <CLI/CLI.hpp>

#include "slang-autos/Tool.h"
#include "slang-autos/Writer.h"

using namespace slang_autos;

int main(int argc, char* argv[]) {
    CLI::App app{"slang-autos - SystemVerilog AUTO macro expander"};
    app.set_version_flag("--version", "0.1.0");

    // ========================================================================
    // CLI Options
    // ========================================================================

    // Files to process
    std::vector<std::string> files;
    app.add_option("files", files, "SystemVerilog files to process");

    // File list option
    std::vector<std::string> file_lists;
    app.add_option("-f,--file-list", file_lists, "File list (.f file)")
        ->check(CLI::ExistingFile);

    // Library directories
    std::vector<std::string> lib_dirs;
    app.add_option("-y,--library-dir", lib_dirs, "Library directory for module lookup");

    // Include directories
    std::vector<std::string> inc_dirs;
    app.add_option("-I,--include-dir", inc_dirs, "Include directory for `include");

    // Output modes
    bool dry_run = false;
    app.add_flag("--dry-run", dry_run, "Show changes without modifying files");

    bool diff_mode = false;
    app.add_flag("--diff", diff_mode, "Output unified diff instead of modifying");

    // Strictness
    bool strict_mode = false;
    app.add_flag("--strict", strict_mode, "Error on missing modules (default: warn and continue)");

    // Formatting options
    int indent_spaces = 4;
    app.add_option("--indent", indent_spaces, "Indentation width in spaces (default: 4)");

    bool no_alignment = false;
    app.add_flag("--no-alignment", no_alignment, "Don't align port names");

    // Verbosity
    int verbosity = 1;
    app.add_flag("-v,--verbose", verbosity, "Increase verbosity (can be repeated)");

    bool quiet = false;
    app.add_flag("-q,--quiet", quiet, "Suppress non-error output");

    // Config file
    std::string config_file;
    app.add_option("--config", config_file, "Configuration file (.slang-autos.toml)")
        ->check(CLI::ExistingFile);

    // ========================================================================
    // EDA-style options (passed through to slang)
    // ========================================================================

    // These are captured as remaining args and passed to slang
    std::vector<std::string> extra_args;
    app.allow_extras();

    // ========================================================================
    // Parse
    // ========================================================================

    CLI11_PARSE(app, argc, argv);

    // Collect extra args (EDA-style options)
    extra_args = app.remaining();

    // Handle quiet flag
    if (quiet) {
        verbosity = 0;
    }

    // ========================================================================
    // Build tool options
    // ========================================================================

    AutosTool::Options options;
    options.strictness = strict_mode ? StrictnessMode::Strict : StrictnessMode::Lenient;
    options.alignment = !no_alignment;
    options.indent = std::string(indent_spaces, ' ');
    options.verbosity = verbosity;

    // ========================================================================
    // Build slang args
    // ========================================================================

    std::vector<std::string> slang_args;

    // Add files
    for (const auto& file : files) {
        slang_args.push_back(file);
    }

    // Add file lists
    for (const auto& f : file_lists) {
        slang_args.push_back("-f");
        slang_args.push_back(f);
    }

    // Add library directories
    for (const auto& dir : lib_dirs) {
        slang_args.push_back("-y");
        slang_args.push_back(dir);
    }

    // Add include directories
    for (const auto& dir : inc_dirs) {
        slang_args.push_back("+incdir+" + dir);
    }

    // Add EDA-style extra args
    for (const auto& arg : extra_args) {
        slang_args.push_back(arg);
    }

    // ========================================================================
    // Run tool
    // ========================================================================

    if (files.empty() && file_lists.empty()) {
        std::cerr << "Error: No input files specified\n";
        std::cerr << "Run with --help for usage information\n";
        return 1;
    }

    AutosTool tool(options);

    // Load design
    if (!tool.loadWithArgs(slang_args)) {
        std::cerr << tool.diagnostics().format();
        return 1;
    }

    // Process each file
    int total_autoinst = 0;
    int total_autowire = 0;
    int files_changed = 0;
    bool any_errors = false;

    for (const auto& file : files) {
        std::filesystem::path path(file);

        if (!std::filesystem::exists(path)) {
            std::cerr << "Error: File not found: " << file << "\n";
            any_errors = true;
            continue;
        }

        if (verbosity >= 2) {
            std::cout << "Processing: " << file << "\n";
        }

        auto result = tool.expandFile(path, dry_run || diff_mode);

        if (!result.success) {
            any_errors = true;
            continue;
        }

        total_autoinst += result.autoinst_count;
        total_autowire += result.autowire_count;

        if (result.hasChanges()) {
            ++files_changed;

            if (diff_mode) {
                SourceWriter writer(true);
                std::cout << writer.generateDiff(path, result.original_content, result.modified_content);
            } else if (verbosity >= 1) {
                std::cout << file << ": "
                          << result.autoinst_count << " AUTOINST, "
                          << result.autowire_count << " AUTOWIRE\n";
            }
        }
    }

    // Print diagnostics
    if (tool.diagnostics().hasErrors() || (verbosity >= 1 && tool.diagnostics().warningCount() > 0)) {
        std::cerr << tool.diagnostics().format();
    }

    // Print summary
    if (verbosity >= 1 && !diff_mode) {
        std::cout << "\nSummary: "
                  << files_changed << " file(s) "
                  << (dry_run ? "would be " : "") << "changed, "
                  << total_autoinst << " AUTOINST, "
                  << total_autowire << " AUTOWIRE\n";
    }

    return any_errors || tool.diagnostics().hasErrors() ? 1 : 0;
}
