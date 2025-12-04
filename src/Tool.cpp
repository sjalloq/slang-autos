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

    // Parse inline configuration (with validation warnings)
    InlineConfig inline_config = parseInlineConfig(result.original_content, file.string(), &diagnostics_);
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

    // ════════════════════════════════════════════════════════════════════════════
    // POST-PROCESSING WORKAROUNDS FOR SLANG'S TRIVIA MODEL
    // ════════════════════════════════════════════════════════════════════════════
    //
    // Slang uses a "leading trivia" model where comments and whitespace attach to
    // the NEXT token, not the preceding one. This creates challenges for AUTO macros:
    //
    //   /*AUTOWIRE*/           <-- This comment is trivia on 'sub', not standalone
    //   sub u_0 (/*AUTOINST*/);
    //
    // When we replace or insert nodes, trivia doesn't always end up where we want.
    // These text-based cleanups handle edge cases that are difficult to solve purely
    // through syntax tree operations.
    //
    // FUTURE IMPROVEMENT: These workarounds could potentially be eliminated by:
    // 1. Using slang's trivia manipulation APIs more carefully (if they exist)
    // 2. Building a custom trivia-aware rewriter that tracks marker positions
    // 3. Pre-processing to convert markers to actual syntax nodes before rewriting
    //
    // For now, the text-based approach is pragmatic and handles all known cases.
    // ════════════════════════════════════════════════════════════════════════════

    // WORKAROUND 1: Remove dummy marker localparam
    //
    // We generate "localparam _SLANG_AUTOS_END_MARKER_ = 0;" as an anchor for the
    // "// End of automatics" comment. In slang's trivia model, comments must attach
    // to a syntax node - they can't exist standalone. The dummy localparam serves as
    // that anchor, allowing the end comment to survive the rewriter transformation.
    // We then remove the dummy here, leaving just the comment.
    //
    // Alternative: Could potentially use slang's trivia APIs to attach the comment
    // directly to the next real syntax node, but this is simpler and reliable.
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

    // WORKAROUND 2: Remove duplicate /*AUTOWIRE*/ markers
    //
    // The /*AUTOWIRE*/ marker can appear twice in output:
    // 1. We explicitly include it in generateAutowireText() so it appears before declarations
    // 2. The original marker (as trivia on AUTOINST node) is preserved via preserveTrivia=true
    //
    // We use preserveTrivia=true on AUTOINST replacement to keep AUTO_TEMPLATE comments,
    // but this also preserves the /*AUTOWIRE*/ that was trivia on that node. Rather than
    // losing the template comments, we accept the duplicate and clean it up here.
    //
    // The first occurrence (before declarations) is kept; any after "// End of automatics"
    // are removed as duplicates.
    //
    // Alternative: Could strip specific trivia items before replacement, but slang's API
    // doesn't make selective trivia removal straightforward.
    const std::string autowire_marker = "/*AUTOWIRE*/";
    const std::string end_marker = "// End of automatics";
    size_t end_pos = result.modified_content.find(end_marker);
    if (end_pos != std::string::npos) {
        // Look for /*AUTOWIRE*/ after the end marker
        pos = result.modified_content.find(autowire_marker, end_pos);
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

            // Check if this line contains only /*AUTOWIRE*/ (plus whitespace)
            std::string line = result.modified_content.substr(line_start, line_end - line_start);
            std::string trimmed = line;
            // Simple whitespace trim
            size_t start = trimmed.find_first_not_of(" \t\n\r");
            size_t end = trimmed.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                trimmed = trimmed.substr(start, end - start + 1);
            }

            if (trimmed == autowire_marker) {
                // Remove this line (duplicate marker)
                result.modified_content.erase(line_start, line_end - line_start);
                // Look for more after the end marker
                end_pos = result.modified_content.find(end_marker);
                if (end_pos == std::string::npos) break;
                pos = result.modified_content.find(autowire_marker, end_pos);
            } else {
                // Not a standalone marker, look for next
                pos = result.modified_content.find(autowire_marker, pos + autowire_marker.length());
            }
        }
    }

    // WORKAROUND 3: Remove duplicate "// End of automatics" markers
    //
    // Similar to workaround 2, the end marker can appear twice:
    // 1. We include it in generateAutowireText() for the fresh expansion
    // 2. The original (from previous expansion) may be preserved as trivia
    //
    // On re-expansion, the old "// End of automatics" was trivia on a node that gets
    // preserved or moved. The safest approach is to generate fresh markers and remove
    // any duplicates here.
    //
    // Alternative: Track which nodes carry old auto-block trivia and strip it before
    // transformation. This would require more complex trivia analysis.
    const std::string end_auto_marker = "// End of automatics";
    size_t first_end = result.modified_content.find(end_auto_marker);
    if (first_end != std::string::npos) {
        // Look for additional occurrences after the first
        pos = result.modified_content.find(end_auto_marker, first_end + end_auto_marker.length());
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

            // Check if this line contains only "// End of automatics" (plus whitespace)
            std::string line = result.modified_content.substr(line_start, line_end - line_start);
            std::string trimmed = line;
            size_t start = trimmed.find_first_not_of(" \t\n\r");
            size_t end = trimmed.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                trimmed = trimmed.substr(start, end - start + 1);
            }

            if (trimmed == end_auto_marker) {
                // Remove this line (duplicate end marker)
                result.modified_content.erase(line_start, line_end - line_start);
                // Continue searching from the first occurrence
                pos = result.modified_content.find(end_auto_marker, first_end + end_auto_marker.length());
            } else {
                // Not a standalone marker, look for next
                pos = result.modified_content.find(end_auto_marker, pos + end_auto_marker.length());
            }
        }
    }

    // WORKAROUND 4: Remove duplicate /*AUTOPORTS*/ markers
    //
    // The /*AUTOPORTS*/ marker can appear twice in output:
    // 1. We include it before the first generated port (correct position)
    // 2. The original marker (as trivia on closeParen) is preserved inline
    //
    // We keep the first occurrence (after user ports, before generated) and remove
    // all subsequent duplicates - whether standalone or inline.
    const std::string autoports_marker = "/*AUTOPORTS*/";
    size_t first_autoports = result.modified_content.find(autoports_marker);
    if (first_autoports != std::string::npos) {
        // Look for additional occurrences after the first
        pos = result.modified_content.find(autoports_marker, first_autoports + autoports_marker.length());
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

            // Check if this line contains only /*AUTOPORTS*/ (plus whitespace)
            std::string line = result.modified_content.substr(line_start, line_end - line_start);
            std::string trimmed = line;
            size_t start = trimmed.find_first_not_of(" \t\n\r");
            size_t end = trimmed.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                trimmed = trimmed.substr(start, end - start + 1);
            }

            if (trimmed == autoports_marker) {
                // Remove this entire line (standalone duplicate marker)
                result.modified_content.erase(line_start, line_end - line_start);
                // Continue searching
                pos = result.modified_content.find(autoports_marker, first_autoports + autoports_marker.length());
            } else {
                // Inline marker (e.g., "input logic foo/*AUTOPORTS*/") - remove just the marker
                result.modified_content.erase(pos, autoports_marker.length());
                // Continue searching from the same position
                pos = result.modified_content.find(autoports_marker, first_autoports + autoports_marker.length());
            }
        }
    }

    // WORKAROUND 5: Clean up multiple consecutive blank lines
    //
    // The above removals can leave behind multiple blank lines where content was stripped.
    // We normalize to at most one blank line between content for cleaner output.
    //
    // This is cosmetic but improves readability of the generated output.
    const std::string double_blank = "\n\n\n";
    pos = result.modified_content.find(double_blank);
    while (pos != std::string::npos) {
        result.modified_content.replace(pos, 3, "\n\n");
        pos = result.modified_content.find(double_blank, pos);
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

} // namespace slang_autos
