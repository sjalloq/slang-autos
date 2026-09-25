#pragma once

#include <set>
#include <string>
#include <unordered_set>
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
class InstanceBodySymbol;
class Scope;
}

namespace slang_autos {

/// Configuration options for AutosAnalyzer
struct AutosAnalyzerOptions {
    bool alignment{};
    std::string indent;
    PortGrouping grouping{};
    StrictnessMode strictness{};
    bool resolved_ranges = false; ///< Use resolved widths instead of original syntax
    std::optional<DirectionComments> direction_comments; ///< Per-port direction arrows (nullopt = disabled)
    NetType net_type = NetType::Logic; ///< Net type for generated declarations
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

    /// Port connection info collected during AST traversal
    struct CollectedPortConnection {
        std::string port_name;
        std::string signal_expr;  ///< For output generation
        std::vector<std::string> signal_identifiers;  ///< Pre-extracted from AST
    };

    /// Information about a manual (non-AUTOINST) instance for signal tracking
    struct ManualInstInfo {
        const slang::syntax::HierarchyInstantiationSyntax* node = nullptr;
        std::string module_type;
        std::string instance_name;
        std::vector<CollectedPortConnection> port_connections;
    };

    /// All information collected from a single module
    struct CollectedInfo {
        std::vector<AutoInstInfo> autoinsts;
        std::vector<ManualInstInfo> manual_insts;  ///< Non-AUTOINST instances for signal tracking
        AutoLogicInfo autologic;
        AutoPortsInfo autoports;
        bool has_autologic = false;
        bool has_autoports = false;
        std::set<std::string> existing_decls;
        /// Nets WRITTEN by this module's own logic (assignment LHS, lvalue/ref
        /// subroutine args) anywhere — procedural blocks, continuous assigns,
        /// initializers. Driven internally, so they must not become input ports.
        std::set<std::string> internally_driven;
        /// Nets READ by this module's own logic (assignment RHS, select indices,
        /// input subroutine args, any rvalue context). Consumed internally, so
        /// they must not become output ports.
        std::set<std::string> internally_consumed;
    };

    // ════════════════════════════════════════════════════════════════════════
    // Analysis phases
    // ════════════════════════════════════════════════════════════════════════

    void processModule(const slang::syntax::ModuleDeclarationSyntax& module);
    CollectedInfo collectModuleInfo(const slang::syntax::ModuleDeclarationSyntax& module);
    void processMemberRecursive(const slang::syntax::MemberSyntax* member,
                                CollectedInfo& info,
                                bool& in_autologic_block,
                                bool in_dead_branch);

    /// Walk the elaborated AST for this module and populate
    /// dead_generate_blocks_ with syntax pointers for every generate block
    /// whose branch was not chosen during elaboration. Used by the parser to
    /// skip assign/declaration tracking inside dead branches — those
    /// statements don't exist in the elaborated design, and recording them
    /// corrupts AUTOPORTS/AUTOLOGIC classification.
    void collectDeadGenerateBlocks(const slang::syntax::ModuleDeclarationSyntax& module);
    void collectDeadBlocksFromScope(const slang::ast::Scope& scope);
    void resolvePortsAndSignals(const slang::syntax::ModuleDeclarationSyntax& module,
                                CollectedInfo& info);
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

    // Returns every declared name in a net/data declaration. A single
    // declaration may declare several signals (e.g. `wire wire_a, wire_b;`),
    // so all declarators must be reported, not just the first.
    std::vector<std::string>
    extractDeclarationNames(const slang::syntax::MemberSyntax& member) const;

    const AutoTemplate* findTemplate(const std::string& module_name,
                                      size_t before_line) const;

    std::string generatePortConnections(const AutoInstInfo& inst,
                                        const std::vector<PortInfo>& ports);
    std::string generateAutologicDecls(const CollectedInfo& info);
    std::string detectIndent(const slang::syntax::SyntaxNode& node) const;

    /// Adapt signal expression for width mismatches.
    /// Applies bit-slicing (port < signal), zero-padding (input port > signal),
    /// or unused signal concatenation (output port > signal).
    /// @param signal The original signal expression
    /// @param port Port info with width and direction
    /// @param match Template match result (to check if template was applied)
    /// @param instance_name Instance name for generating unused signal names
    /// @return Adapted signal expression
    std::string adaptSignalWidth(const std::string& signal,
                                  const PortInfo& port,
                                  const MatchResult& match,
                                  const std::string& instance_name);

    /// Returns true if original syntax should be preserved (opposite of resolved_ranges)
    [[nodiscard]] bool preferOriginalSyntax() const { return !options_.resolved_ranges; }

    /// Find the last source character before @p end that is real code, i.e. the
    /// last character of the preceding port connection or declaration. Comments
    /// (`//` and `/* */`), string literals and preprocessor directive lines
    /// (e.g. `ifdef / `endif) are skipped, so trailing per-port comments and
    /// conditional-compilation blocks between the last port and an AUTO marker
    /// don't hide its trailing comma.
    /// @param end Source offset to scan up to (exclusive)
    /// @return Offset of the last real character, or nullopt if there is none
    [[nodiscard]] std::optional<size_t> lastCodeCharBefore(size_t end) const;

    /// Decide whether a comma must be inserted before generated ports, by
    /// looking at the last real character before an AUTO marker.
    /// @param marker_start Source offset of the marker's first character
    /// @return true if the preceding real content does not end in a comma
    [[nodiscard]] bool needsLeadingComma(size_t marker_start) const;

    // ════════════════════════════════════════════════════════════════════════
    // Member data
    // ════════════════════════════════════════════════════════════════════════

    slang::ast::Compilation& compilation_;
    const std::vector<AutoTemplate>& templates_;
    AutosAnalyzerOptions options_;
    SignalAggregator aggregator_;

    std::string_view source_content_;  // Original source for comparison
    std::vector<Replacement> replacements_;

    /// Source offsets of generate blocks pruned by elaboration for the
    /// current module. Populated per-module by collectDeadGenerateBlocks().
    /// Offsets rather than syntax pointers because Tool.cpp re-parses the
    /// source independently of the slang compilation's tree, so pointer
    /// equality can't cross that boundary — but offsets into the same
    /// source text are consistent.
    std::unordered_set<size_t> dead_generate_blocks_;

    int autoinst_count_ = 0;
    int autologic_count_ = 0;
    int autoports_count_ = 0;
};

} // namespace slang_autos
