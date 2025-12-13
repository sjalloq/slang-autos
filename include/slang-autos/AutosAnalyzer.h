#pragma once

#include <set>
#include <string>
#include <vector>
#include <optional>

#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/parsing/Token.h>

#include "Diagnostics.h"
#include "SignalAggregator.h"
#include "Parser.h"
#include "TemplateMatcher.h"
#include "Writer.h"

namespace slang::ast {
class Compilation;
}

namespace slang_autos {

/// Configuration options for AutosAnalyzer
struct AutosAnalyzerOptions {
    bool alignment{};
    std::string indent;
    PortGrouping grouping{};
    StrictnessMode strictness{};
    DiagnosticCollector* diagnostics = nullptr;
};

/// Analyzes SystemVerilog modules and generates text replacements for AUTO macros.
///
/// ## Design
///
/// Uses AST for analysis only - all modifications are done via text replacement
/// to preserve whitespace and formatting perfectly. All position information
/// comes from the AST (token locations, trivia offsets) - we never search the
/// raw source text to find markers or boundaries.
///
/// ## Usage
///
/// ```cpp
/// AutosAnalyzer analyzer(compilation, templates, options);
/// analyzer.analyze(tree, source_content);
/// auto& replacements = analyzer.getReplacements();
/// std::string output = writer.applyReplacements(original_source, replacements);
/// ```
class AutosAnalyzer {
public:
    AutosAnalyzer(slang::ast::Compilation& compilation,
                  const std::vector<AutoTemplate>& templates,
                  const AutosAnalyzerOptions& options = {});

    /// Analyze a syntax tree and collect all pending replacements.
    /// Does NOT modify the tree - just collects information and generates
    /// replacement instructions.
    /// @param tree The syntax tree to analyze
    /// @param source_content Original source text (for comparing replacements)
    void analyze(const std::shared_ptr<slang::syntax::SyntaxTree>& tree,
                 std::string_view source_content);

    /// Get collected replacements. Apply to original source with SourceWriter.
    [[nodiscard]] std::vector<Replacement>& getReplacements() { return replacements_; }
    [[nodiscard]] const std::vector<Replacement>& getReplacements() const { return replacements_; }

    [[nodiscard]] int autoinstCount() const { return autoinst_count_; }
    [[nodiscard]] int autologicCount() const { return autologic_count_; }
    [[nodiscard]] int autoportsCount() const { return autoports_count_; }

private:
    // ════════════════════════════════════════════════════════════════════════
    // Collection structures - positions from AST
    // ════════════════════════════════════════════════════════════════════════

    /// Information about an AUTOINST marker and its source location
    struct AutoInstInfo {
        const slang::syntax::MemberSyntax* node = nullptr;
        std::string module_type;
        std::string instance_name;
        std::set<std::string> manual_ports;
        const AutoTemplate* templ = nullptr;

        // Positions from AST - replace from marker_end to close_paren_pos
        size_t marker_end = 0;
        size_t close_paren_pos = 0;
    };

    /// Information about AUTOLOGIC marker and any existing expansion block
    struct AutoLogicInfo {
        size_t marker_end = 0;
        bool has_existing_block = false;
        size_t block_start = 0;
        size_t block_end = 0;
    };

    /// Information about AUTOPORTS marker and port list bounds
    struct AutoPortsInfo {
        size_t marker_end = 0;
        size_t close_paren_pos = 0;
        std::set<std::string> existing_ports;
    };

    /// All information collected from a single module
    struct CollectedInfo {
        std::vector<AutoInstInfo> autoinsts;
        AutoLogicInfo autologic;
        AutoPortsInfo autoports;
        bool has_autologic = false;
        bool has_autoports = false;
        std::set<std::string> existing_decls;
    };

    // ════════════════════════════════════════════════════════════════════════
    // Analysis phases
    // ════════════════════════════════════════════════════════════════════════

    void processModule(const slang::syntax::ModuleDeclarationSyntax& module);
    CollectedInfo collectModuleInfo(const slang::syntax::ModuleDeclarationSyntax& module);
    void resolvePortsAndSignals(CollectedInfo& info);
    void generateReplacements(const slang::syntax::ModuleDeclarationSyntax& module,
                              const CollectedInfo& info);

    // ════════════════════════════════════════════════════════════════════════
    // Replacement generators
    // ════════════════════════════════════════════════════════════════════════

    void generateAutoInstReplacement(const AutoInstInfo& inst,
                                     const std::vector<PortInfo>& ports);
    void generateAutologicReplacement(const CollectedInfo& info);
    void generateAutoportsReplacement(const slang::syntax::ModuleDeclarationSyntax& module,
                                      const CollectedInfo& info);

    // ════════════════════════════════════════════════════════════════════════
    // AST position helpers - all position finding goes through these
    // ════════════════════════════════════════════════════════════════════════

    /// Check if token trivia contains a marker
    bool hasMarkerInTokenTrivia(slang::parsing::Token tok, std::string_view marker) const;

    /// Find marker in token trivia, return {start, end} offsets
    std::optional<std::pair<size_t, size_t>>
    findMarkerInTrivia(slang::parsing::Token tok, std::string_view marker) const;

    /// Check all tokens in a node for a marker
    bool hasMarker(const slang::syntax::SyntaxNode& node, std::string_view marker) const;

    /// Find marker anywhere in node's tokens/trivia, return {start, end} offsets
    std::optional<std::pair<size_t, size_t>>
    findMarkerInNode(const slang::syntax::SyntaxNode& node, std::string_view marker) const;

    // ════════════════════════════════════════════════════════════════════════
    // Other helpers
    // ════════════════════════════════════════════════════════════════════════

    std::vector<PortInfo> getModulePorts(const std::string& module_name);
    std::vector<PortConnection> buildConnections(const AutoInstInfo& inst,
                                                  const std::vector<PortInfo>& ports);

    std::optional<std::pair<std::string, std::string>>
    extractInstanceInfo(const slang::syntax::MemberSyntax& member) const;

    std::optional<std::string>
    extractDeclarationName(const slang::syntax::MemberSyntax& member) const;

    const AutoTemplate* findTemplate(const std::string& module_name,
                                      size_t before_line) const;

    std::string generatePortConnections(const AutoInstInfo& inst,
                                        const std::vector<PortInfo>& ports);
    std::string generateAutologicDecls(const std::set<std::string>& existing_decls);
    std::string detectIndent(const slang::syntax::SyntaxNode& node) const;

    // ════════════════════════════════════════════════════════════════════════
    // Member data
    // ════════════════════════════════════════════════════════════════════════

    slang::ast::Compilation& compilation_;
    const std::vector<AutoTemplate>& templates_;
    AutosAnalyzerOptions options_;
    SignalAggregator aggregator_;

    std::string_view source_content_;  // Original source for comparison
    std::vector<Replacement> replacements_;

    int autoinst_count_ = 0;
    int autologic_count_ = 0;
    int autoports_count_ = 0;
};

} // namespace slang_autos
