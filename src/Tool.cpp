#include "slang-autos/Tool.h"

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

    // Parse AUTO comments
    AutoParser parser(&diagnostics_);
    parser.parseText(result.original_content, file.string());

    // Collect all expanded signals for AUTOWIRE
    std::vector<ExpandedSignal> all_signals;

    // Process AUTOINSTs first (to collect signals for AUTOWIRE)
    for (const auto& autoinst : parser.autoinsts()) {
        auto expansion = expandAutoInst(result.modified_content, autoinst, parser);
        if (expansion) {
            result.replacements.push_back(expansion->first);
            all_signals.insert(all_signals.end(),
                              expansion->second.begin(),
                              expansion->second.end());
            ++result.autoinst_count;
        }
    }

    // Process AUTOWIREs
    for (const auto& autowire : parser.autowires()) {
        auto replacement = expandAutoWire(result.modified_content, autowire, all_signals);
        if (replacement) {
            result.replacements.push_back(*replacement);
            ++result.autowire_count;
        }
    }

    // Apply all replacements
    if (!result.replacements.empty()) {
        SourceWriter writer(dry_run);
        result.modified_content = writer.applyReplacements(
            result.original_content, result.replacements);

        // Write file if not dry run
        if (!dry_run) {
            writer.writeFile(file, result.modified_content);
        }
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

std::optional<Replacement> AutosTool::expandAutoWire(
    const std::string& content,
    const AutoWire& autowire,
    const std::vector<ExpandedSignal>& all_signals) {

    // Filter signals to those from instances after this AUTOWIRE
    // (In full implementation, would check line numbers)
    std::vector<ExpandedSignal> relevant_signals;
    for (const auto& sig : all_signals) {
        // For now, include all output signals
        if (sig.direction == "output") {
            relevant_signals.push_back(sig);
        }
    }

    // Find existing declarations
    auto existing = findExistingDeclarations(content, autowire.source_offset);

    // Detect indent
    std::string indent = detectIndent(content, autowire.source_offset);
    if (indent.empty()) {
        indent = options_.indent;
    }

    // Expand
    AutoWireExpander expander(&diagnostics_);
    std::string expansion = expander.expand(relevant_signals, existing, indent);

    if (expansion.empty()) {
        return std::nullopt;
    }

    // Find end of existing AUTOWIRE region
    // Look for "// End of automatic wires" or next statement
    size_t end_offset = autowire.end_offset;

    auto end_marker = content.find("// End of automatic wires", autowire.end_offset);
    if (end_marker != std::string::npos) {
        // Include the marker line
        auto line_end = content.find('\n', end_marker);
        end_offset = (line_end != std::string::npos) ? line_end + 1 : content.length();
    }

    // Create replacement
    Replacement repl;
    repl.start = autowire.end_offset;
    repl.end = end_offset;
    repl.new_text = expansion;
    repl.description = "AUTOWIRE";

    return repl;
}

} // namespace slang_autos
