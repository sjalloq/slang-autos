#include "slang-autos/Tool.h"
#include "slang-autos/AutosAnalyzer.h"
#include "slang-autos/Constants.h"
#include "slang-autos/Writer.h"

#include <fstream>
#include <sstream>

#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

namespace slang_autos {

AutosTool::AutosTool()
    : options_{} {
}

AutosTool::AutosTool(const Options& options)
    : options_(options) {
}

AutosTool::~AutosTool() = default;

AutosTool::AutosTool(AutosTool&&) noexcept = default;
AutosTool& AutosTool::operator=(AutosTool&&) noexcept = default;

bool AutosTool::loadWithArgs(const std::vector<std::string>& args) {
    // Create driver
    driver_ = std::make_unique<slang::driver::Driver>();

    // Register standard slang options (-f, -y, +incdir+, etc.)
    driver_->addStandardArgs();

    // Convert args to argc/argv style
    std::vector<const char*> argv;
    argv.push_back("slang-autos");  // Program name
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }

    // Parse arguments through slang driver
    if (!driver_->parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        diagnostics_.addError("Failed to parse command line arguments");
        return false;
    }

    // Process options (handles -f files, library paths, etc.)
    if (!driver_->processOptions()) {
        diagnostics_.addError("Failed to process options");
        return false;
    }

    // Parse all sources (including library files on demand)
    if (!driver_->parseAllSources()) {
        diagnostics_.addError("Failed to parse sources");
        return false;
    }

    // Create compilation using driver (properly handles library resolution)
    compilation_ = driver_->createCompilation();

    return true;
}

void AutosTool::setCompilation(std::unique_ptr<slang::ast::Compilation> compilation) {
    compilation_ = std::move(compilation);
    port_cache_.clear();
}

ExpansionResult AutosTool::expandFile(
    const std::filesystem::path& file,
    bool dry_run) {

    ExpansionResult result;

    // ─────────────────────────────────────────────────────────────────────────
    // Read source file
    // ─────────────────────────────────────────────────────────────────────────
    std::ifstream ifs(file);
    if (!ifs) {
        diagnostics_.addError("Failed to open file: " + file.string());
        result.success = false;
        return result;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    result.original_content = buffer.str();

    if (!compilation_) {
        diagnostics_.addError("No compilation available - call loadWithArgs first");
        result.success = false;
        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Parse AUTO templates from comments
    // ─────────────────────────────────────────────────────────────────────────
    AutoParser parser(&diagnostics_);
    parser.parseText(result.original_content, file.string());

    // ─────────────────────────────────────────────────────────────────────────
    // Get configuration
    // ─────────────────────────────────────────────────────────────────────────
    InlineConfig inline_config = getInlineConfig(file);

    // ─────────────────────────────────────────────────────────────────────────
    // Parse source to AST (read-only, for analysis)
    // ─────────────────────────────────────────────────────────────────────────
    auto tree = slang::syntax::SyntaxTree::fromText(result.original_content);
    if (!tree) {
        diagnostics_.addError("Failed to parse file as SystemVerilog");
        result.success = false;
        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Configure analyzer
    // ─────────────────────────────────────────────────────────────────────────
    AutosAnalyzerOptions opts;
    opts.alignment = inline_config.alignment.value_or(options_.alignment);

    if (inline_config.indent.has_value()) {
        int indent_val = *inline_config.indent;
        if (indent_val == -1) {
            opts.indent = "\t";
        } else {
            opts.indent = std::string(static_cast<size_t>(indent_val), ' ');
        }
    } else {
        opts.indent = options_.indent;
    }

    opts.grouping = inline_config.grouping.value_or(PortGrouping::ByDirection);
    opts.strictness = inline_config.strictness.value_or(options_.strictness);
    opts.resolved_ranges = inline_config.resolved_ranges.value_or(options_.resolved_ranges);
    opts.diagnostics = &diagnostics_;

    // ─────────────────────────────────────────────────────────────────────────
    // Analyze and collect replacements
    // ─────────────────────────────────────────────────────────────────────────
    AutosAnalyzer analyzer(*compilation_, parser.templates(), opts);
    analyzer.analyze(tree, result.original_content);

    // ─────────────────────────────────────────────────────────────────────────
    // Apply replacements to original source
    // ─────────────────────────────────────────────────────────────────────────
    auto& replacements = analyzer.getReplacements();

    if (replacements.empty()) {
        result.modified_content = result.original_content;
    } else {
        SourceWriter writer(false);
        result.modified_content = writer.applyReplacements(result.original_content, replacements);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Update statistics
    // ─────────────────────────────────────────────────────────────────────────
    result.autoinst_count = analyzer.autoinstCount();
    result.autologic_count = analyzer.autologicCount();

    // ─────────────────────────────────────────────────────────────────────────
    // Write output
    // ─────────────────────────────────────────────────────────────────────────
    if (!dry_run && result.hasChanges()) {
        SourceWriter writer(false);
        writer.writeFile(file, result.modified_content);
    }

    return result;
}

std::vector<PortInfo> AutosTool::getModulePorts(const std::string& module_name) {
    // Check cache first
    auto cache_it = port_cache_.find(module_name);
    if (cache_it != port_cache_.end()) {
        return cache_it->second;
    }

    if (!compilation_) {
        return {};
    }

    // Use shared implementation
    auto ports = getModulePortsFromCompilation(
        *compilation_, module_name, &diagnostics_, options_.strictness);

    // Cache result (including empty results to avoid repeated failed lookups)
    port_cache_[module_name] = ports;
    return ports;
}

void AutosTool::setInlineConfig(const std::filesystem::path& file, const InlineConfig& config) {
    inline_configs_[file.string()] = config;
}

InlineConfig AutosTool::getInlineConfig(const std::filesystem::path& file) const {
    auto it = inline_configs_.find(file.string());
    if (it != inline_configs_.end()) {
        return it->second;
    }
    return InlineConfig{};
}

} // namespace slang_autos
