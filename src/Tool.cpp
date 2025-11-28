#include "slang-autos/Tool.h"
#include "slang-autos/AutosRewriter.h"

#include <fstream>
#include <set>
#include <sstream>

// slang includes
#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxPrinter.h"

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
    port_cache_.clear();  // Clear cache when compilation changes
}

ExpansionResult AutosTool::expandFile(
    const std::filesystem::path& file,
    bool dry_run) {

    ExpansionResult result;

    // Read file content
    std::ifstream ifs(file);
    if (!ifs) {
        diagnostics_.addError("Failed to open file: " + file.string());
        result.success = false;
        return result;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    result.original_content = buffer.str();
    result.modified_content = result.original_content;

    // Check if we have a compilation
    if (!compilation_) {
        diagnostics_.addError("No compilation available - call loadWithArgs first");
        result.success = false;
        return result;
    }

    // Parse AUTO comments (still need this for template extraction)
    AutoParser parser(&diagnostics_);
    parser.parseText(result.original_content, file.string());

    // Parse inline configuration
    InlineConfig inline_config = parseInlineConfig(result.original_content);
    PortGrouping grouping = inline_config.grouping.value_or(PortGrouping::ByDirection);

    // Parse the source with slang's SyntaxTree
    using namespace slang::syntax;
    auto tree = SyntaxTree::fromText(result.original_content);
    if (!tree) {
        diagnostics_.addError("Failed to parse file as SystemVerilog");
        result.success = false;
        return result;
    }

    // Configure rewriter options
    AutosRewriterOptions rewriter_opts;
    rewriter_opts.use_logic = true;
    rewriter_opts.alignment = options_.alignment;
    rewriter_opts.indent = options_.indent;
    rewriter_opts.grouping = grouping;
    rewriter_opts.strictness = options_.strictness;
    rewriter_opts.diagnostics = &diagnostics_;

    // Create unified rewriter with templates from parser
    AutosRewriter rewriter(*compilation_, parser.templates(), rewriter_opts);

    // Transform the tree (handles AUTOINST, AUTOWIRE, AUTOREG in one pass)
    auto new_tree = rewriter.transform(tree);

    // Convert back to text
    result.modified_content = SyntaxPrinter::printFile(*new_tree);

    // Remove the dummy marker localparam that was added to preserve trivia
    const std::string dummy_marker = "localparam _SLANG_AUTOS_END_MARKER_ = 0;";
    size_t pos = result.modified_content.find(dummy_marker);
    while (pos != std::string::npos) {
        // Find the start of the line
        size_t line_start = result.modified_content.rfind('\n', pos);
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;

        // Find the end of the line
        size_t line_end = result.modified_content.find('\n', pos);
        if (line_end != std::string::npos) {
            line_end++;  // Include the newline
        } else {
            line_end = result.modified_content.length();
        }

        // Remove the line
        result.modified_content.erase(line_start, line_end - line_start);

        // Look for more occurrences
        pos = result.modified_content.find(dummy_marker);
    }

    // Count expansions (for reporting)
    // The rewriter handles these internally, so we just check if content changed
    if (result.hasChanges()) {
        result.autoinst_count = static_cast<int>(parser.autoinsts().size());
        result.autowire_count = static_cast<int>(parser.autowires().size());
    }

    // Write file if not dry run and there were changes
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

    std::vector<PortInfo> ports;

    if (!compilation_) {
        return ports;
    }

    // Get the root and find instances of the target module
    // In typical usage, we compile the target file as top, so submodules
    // are direct children of the top instance(s)
    auto& root = compilation_->getRoot();

    const slang::ast::InstanceBodySymbol* found_body = nullptr;

    // Iterate top instances (typically just the module being processed)
    for (auto* topInst : root.topInstances) {
        // Look in the top module's body for instances of target module
        for (auto& member : topInst->body.members()) {
            if (auto* inst = member.as_if<slang::ast::InstanceSymbol>()) {
                if (inst->body.name == module_name) {
                    found_body = &inst->body;
                    break;
                }
            }
        }
        if (found_body) break;
    }

    if (!found_body) {
        if (options_.strictness == StrictnessMode::Strict) {
            diagnostics_.addError("Module not found: " + module_name);
        } else {
            diagnostics_.addWarning("Module not found: " + module_name);
        }
        // Cache empty result to avoid repeated failed lookups
        port_cache_[module_name] = ports;
        return ports;
    }

    // Extract ports from the body's port list
    for (auto* port : found_body->getPortList()) {
        PortInfo info;
        info.name = std::string(port->name);

        // Get direction
        if (auto* portSym = port->as_if<slang::ast::PortSymbol>()) {
            switch (portSym->direction) {
                case slang::ast::ArgumentDirection::In:
                    info.direction = "input";
                    break;
                case slang::ast::ArgumentDirection::Out:
                    info.direction = "output";
                    break;
                case slang::ast::ArgumentDirection::InOut:
                    info.direction = "inout";
                    break;
                default:
                    info.direction = "input";
                    break;
            }

            // Get type information
            auto& type = portSym->getType();
            info.width = type.getBitWidth();

            // Get range string for packed arrays
            if (type.isPackedArray()) {
                auto& packed = type.getCanonicalType().as<slang::ast::PackedArrayType>();
                auto range = packed.range;
                info.range_str = "[" + std::to_string(range.left) + ":" +
                                std::to_string(range.right) + "]";
            } else if (info.width > 1) {
                info.range_str = "[" + std::to_string(info.width - 1) + ":0]";
            }
        }

        ports.push_back(info);
    }

    // Cache result for future lookups
    port_cache_[module_name] = ports;
    return ports;
}

std::optional<std::pair<Replacement, std::vector<ExpandedSignal>>>
AutosTool::expandAutoInst(
    const std::string& content,
    const AutoInst& autoinst,
    const AutoParser& parser) {

    // Find instance info
    auto info = findInstanceInfoFromAutoinst(content, autoinst.source_offset);
    if (!info) {
        diagnostics_.addWarning(
            "Could not find instance info for AUTOINST",
            autoinst.file_path, autoinst.line_number);
        return std::nullopt;
    }

    auto [module_type, instance_name, inst_start] = *info;

    // Find closing paren
    auto close_paren = findInstanceCloseParen(content, autoinst.end_offset);
    if (!close_paren) {
        diagnostics_.addWarning(
            "Could not find closing paren for instance",
            autoinst.file_path, autoinst.line_number);
        return std::nullopt;
    }

    // Get module ports
    auto ports = getModulePorts(module_type);

    // If no ports found (module not in compilation), preserve existing content
    if (ports.empty()) {
        diagnostics_.addWarning(
            "No ports found for module '" + module_type + "', preserving existing content",
            autoinst.file_path, autoinst.line_number);
        return std::nullopt;
    }

    // Find manual ports
    auto manual_ports = findManualPorts(content, autoinst.source_offset);

    // Find template
    auto* tmpl = parser.getTemplateForModule(module_type, autoinst.line_number);

    // Detect indent
    std::string indent = detectIndent(content, autoinst.source_offset);
    if (indent.empty()) {
        indent = options_.indent;
    }

    // Create expander and expand
    AutoInstExpander expander(tmpl, &diagnostics_);
    std::string expansion = expander.expand(
        instance_name,
        ports,
        manual_ports,
        autoinst.filter_pattern.value_or(""),
        indent,
        options_.alignment);

    // Get expanded signals for AUTOWIRE
    auto signals = expander.getExpandedSignals(instance_name, ports);

    // Create replacement
    Replacement repl;
    repl.start = autoinst.end_offset;
    repl.end = *close_paren;
    repl.new_text = expansion;
    repl.description = "AUTOINST for " + instance_name;

    return std::make_pair(repl, signals);
}

std::optional<Replacement> AutosTool::expandAutoInstWithAggregator(
    const std::string& content,
    const AutoInst& autoinst,
    const AutoParser& parser,
    SignalAggregator& aggregator) {

    // Find instance info
    auto info = findInstanceInfoFromAutoinst(content, autoinst.source_offset);
    if (!info) {
        diagnostics_.addWarning(
            "Could not find instance info for AUTOINST",
            autoinst.file_path, autoinst.line_number);
        return std::nullopt;
    }

    auto [module_type, instance_name, inst_start] = *info;

    // Find closing paren
    auto close_paren = findInstanceCloseParen(content, autoinst.end_offset);
    if (!close_paren) {
        diagnostics_.addWarning(
            "Could not find closing paren for instance",
            autoinst.file_path, autoinst.line_number);
        return std::nullopt;
    }

    // Get module ports
    auto ports = getModulePorts(module_type);

    // If no ports found (module not in compilation), preserve existing content
    if (ports.empty()) {
        diagnostics_.addWarning(
            "No ports found for module '" + module_type + "', preserving existing content",
            autoinst.file_path, autoinst.line_number);
        return std::nullopt;
    }

    // Find manual ports
    auto manual_ports = findManualPorts(content, autoinst.source_offset);

    // Find template
    auto* tmpl = parser.getTemplateForModule(module_type, autoinst.line_number);

    // Detect indent
    std::string indent = detectIndent(content, autoinst.source_offset);
    if (indent.empty()) {
        indent = options_.indent;
    }

    // Create expander and expand
    AutoInstExpander expander(tmpl, &diagnostics_);
    std::string expansion = expander.expand(
        instance_name,
        ports,
        manual_ports,
        autoinst.filter_pattern.value_or(""),
        indent,
        options_.alignment);

    // Add connections to aggregator
    aggregator.addFromInstance(instance_name, expander.connections(), ports);

    // Create replacement
    Replacement repl;
    repl.start = autoinst.end_offset;
    repl.end = *close_paren;
    repl.new_text = expansion;
    repl.description = "AUTOINST for " + instance_name;

    return repl;
}

std::optional<Replacement> AutosTool::expandAutoReg(
    const std::string& content,
    const AutoReg& autoreg,
    const std::vector<NetInfo>& module_outputs,
    const SignalAggregator& aggregator,
    const std::set<std::string>& user_decls) {

    // Detect indent
    std::string indent = detectIndent(content, autoreg.source_offset);
    if (indent.empty()) {
        indent = options_.indent;
    }

    // Expand
    AutoRegExpander expander(&diagnostics_);
    std::string expansion = expander.expand(
        module_outputs, aggregator, user_decls, "logic", indent, PortGrouping::ByDirection);

    if (expansion.empty()) {
        return std::nullopt;
    }

    // Find end of existing AUTOREG region
    size_t end_offset = findAutoBlockEnd(content, autoreg.end_offset, "regs");

    // Create replacement
    Replacement repl;
    repl.start = autoreg.end_offset;
    repl.end = end_offset;
    repl.new_text = expansion;
    repl.description = "AUTOREG";

    return repl;
}

size_t AutosTool::findAutoBlockEnd(const std::string& content, size_t start, [[maybe_unused]] const std::string& marker_suffix) {
    // Look for the standard end marker (verilog-mode compatible)
    std::string end_marker = "// End of automatics";
    auto marker_pos = content.find(end_marker, start);

    if (marker_pos != std::string::npos) {
        // Include the marker line
        auto line_end = content.find('\n', marker_pos);
        return (line_end != std::string::npos) ? line_end + 1 : content.length();
    }

    // No marker found, just return start position
    return start;
}

std::string AutosTool::applyAutowireRewriter(
    const std::string& content,
    const SignalAggregator& aggregator,
    const std::set<std::string>& user_decls) {

    using namespace slang::syntax;

    // Parse content with slang
    auto tree = SyntaxTree::fromText(content);
    if (!tree) {
        diagnostics_.addWarning("Failed to parse content for AUTOWIRE rewriting");
        return content;
    }

    // Create rewriter and transform
    AutowireRewriter rewriter(aggregator, user_decls, true /* use_logic */);
    auto new_tree = rewriter.transform(tree);

    // Convert back to text
    std::string result = SyntaxPrinter::printFile(*new_tree);

    // Remove the dummy marker localparam that was added to preserve the end comment trivia
    const std::string dummy_marker = "localparam _SLANG_AUTOS_END_MARKER_ = 0;";
    size_t pos = result.find(dummy_marker);
    while (pos != std::string::npos) {
        // Find the start of the line
        size_t line_start = result.rfind('\n', pos);
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;

        // Find the end of the line
        size_t line_end = result.find('\n', pos);
        if (line_end != std::string::npos) {
            line_end++;  // Include the newline
        } else {
            line_end = result.length();
        }

        // Remove the line (keeping only whitespace before the marker if it's just indentation)
        result.erase(line_start, line_end - line_start);

        // Look for more occurrences
        pos = result.find(dummy_marker);
    }

    return result;
}

} // namespace slang_autos
