#pragma once

#include <set>
#include <string>
#include <vector>
#include <optional>

#include <slang/syntax/SyntaxVisitor.h>
#include <slang/syntax/AllSyntax.h>

#include "Diagnostics.h"  // For DiagnosticCollector
#include "Expander.h"  // For SignalAggregator, PortInfo, etc.
#include "Parser.h"    // For AutoTemplate
#include "TemplateMatcher.h"  // For template @ substitution

// Forward declarations
namespace slang::ast {
class Compilation;
}

// StrictnessMode is defined in Diagnostics.h (already included above)

namespace slang_autos {

/// Configuration options for AutosRewriter
/// NOTE: Values should be set from AutosToolOptions (which comes from MergedConfig)
struct AutosRewriterOptions {
    bool use_logic{};               ///< Use 'logic' instead of 'wire'
    bool alignment{};               ///< Align port names
    std::string indent;             ///< Indentation string (set from config)
    PortGrouping grouping{};        ///< Port grouping mode
    StrictnessMode strictness{};    ///< Error handling mode
    DiagnosticCollector* diagnostics = nullptr;  ///< Optional diagnostics collector
};

/// Unified rewriter that handles all AUTO macro expansions in a single pass.
///
/// This class extends slang's SyntaxRewriter to process AUTOINST, AUTOWIRE,
/// AUTOREG, and AUTOPORTS in one tree traversal:
///
/// 1. COLLECT: Single iteration over module members to find all AUTO markers
/// 2. RESOLVE: Batch port lookups and signal aggregation
/// 3. GENERATE: Create all expansions with complete knowledge
/// 4. APPLY: transform() applies all changes atomically
///
/// ## Trivia Model Considerations
///
/// Slang uses a "leading trivia" model where comments attach to the NEXT token.
/// This affects AUTO marker handling - see detailed comments in AutosRewriter.cpp
/// and Tool.cpp for workarounds. Key points:
///
/// - AUTO markers in comments become trivia on the following syntax node
/// - Tool.cpp performs post-processing to clean up duplicate markers
/// - A dummy localparam anchors the "// End of automatics" comment
///
/// ## Known Limitations
///
/// - AUTOREG marker reordering: When /*AUTOREG*/ and /*AUTOWIRE*/ are both trivia
///   on the same node, AUTOREG may end up after the AUTOWIRE block in output.
///   This is a trivia ordering issue that would require significant work to fix.
///
/// - Edge case with marker at module end: If /*AUTOWIRE*/ is trivia on the last
///   node before endmodule, insertion may not work correctly.
class AutosRewriter : public slang::syntax::SyntaxRewriter<AutosRewriter> {
public:
    /// Construct a rewriter with compilation context.
    /// @param compilation The slang compilation for port lookups
    /// @param templates Template rules for port renaming
    /// @param options Configuration options
    AutosRewriter(slang::ast::Compilation& compilation,
                  const std::vector<AutoTemplate>& templates,
                  const AutosRewriterOptions& options = {});

    /// Handle module declarations - this is where all AUTO processing happens.
    /// Called by the visitor pattern for each module in the tree.
    void handle(const slang::syntax::ModuleDeclarationSyntax& module);

private:
    // ════════════════════════════════════════════════════════════════════════
    // Phase 1: Collection structures
    // ════════════════════════════════════════════════════════════════════════

    /// Information about an AUTOINST marker
    struct AutoInstInfo {
        const slang::syntax::MemberSyntax* node = nullptr;  ///< The instance node
        std::string module_type;                             ///< Module being instantiated
        std::string instance_name;                           ///< Instance name
        std::set<std::string> manual_ports;                  ///< Manually connected ports
        const AutoTemplate* templ = nullptr;                 ///< Template rules (nullable)
        size_t close_paren_offset = 0;                       ///< Position of closing )
    };

    /// All information collected from a single module
    struct CollectedInfo {
        std::vector<AutoInstInfo> autoinsts;

        // Marker positions (for module body declarations)
        const slang::syntax::MemberSyntax* autowire_marker = nullptr;
        const slang::syntax::MemberSyntax* autoreg_marker = nullptr;

        // Node after the marker (for fresh insertion)
        const slang::syntax::MemberSyntax* autowire_next = nullptr;
        const slang::syntax::MemberSyntax* autoreg_next = nullptr;

        // Existing auto blocks (for re-expansion)
        std::vector<const slang::syntax::MemberSyntax*> autowire_block;
        std::vector<const slang::syntax::MemberSyntax*> autoreg_block;
        const slang::syntax::MemberSyntax* autowire_block_end = nullptr;
        const slang::syntax::MemberSyntax* autoreg_block_end = nullptr;

        // Node after auto block (for re-expansion insert point)
        const slang::syntax::MemberSyntax* autowire_after = nullptr;
        const slang::syntax::MemberSyntax* autoreg_after = nullptr;

        // User declarations (to skip)
        std::set<std::string> existing_decls;

        // AUTOPORTS - for ANSI-style port list expansion
        bool has_autoports = false;                             ///< Whether /*AUTOPORTS*/ was found
        bool autoports_on_closeparen = false;                   ///< True if marker is on closeParen, false if on first port
        const slang::syntax::AnsiPortListSyntax* ansi_ports = nullptr;  ///< The ANSI port list (if any)
        std::set<std::string> existing_ports;                   ///< Manually declared port names (before marker)
        std::vector<const slang::syntax::MemberSyntax*> autogenerated_ports;  ///< Ports after marker (to remove on re-expand)
    };

    /// Collect all AUTO markers and declarations from a module.
    CollectedInfo collectModuleInfo(const slang::syntax::ModuleDeclarationSyntax& module);

    // ════════════════════════════════════════════════════════════════════════
    // Phase 2: Resolution
    // ════════════════════════════════════════════════════════════════════════

    /// Resolve port information and build signal aggregation.
    void resolvePortsAndSignals(CollectedInfo& info);

    /// Get port information for a module from compilation.
    std::vector<PortInfo> getModulePorts(const std::string& module_name);

    /// Build port connections for an instance.
    std::vector<PortConnection> buildConnections(
        const AutoInstInfo& inst,
        const std::vector<PortInfo>& ports);

    // ════════════════════════════════════════════════════════════════════════
    // Phase 3: Generation and queuing
    // ════════════════════════════════════════════════════════════════════════

    /// Queue AUTOINST expansion for an instance.
    void queueAutoInstExpansion(const AutoInstInfo& inst,
                                 const std::vector<PortInfo>& ports);

    /// Queue AUTOWIRE expansion.
    void queueAutowireExpansion(const CollectedInfo& info);

    /// Queue AUTOREG expansion.
    void queueAutoregExpansion(const CollectedInfo& info);

    /// Queue AUTOPORTS expansion (ANSI-style port list).
    void queueAutoportsExpansion(const slang::syntax::ModuleDeclarationSyntax& module,
                                  const CollectedInfo& info);

    // ════════════════════════════════════════════════════════════════════════
    // Helpers
    // ════════════════════════════════════════════════════════════════════════

    /// Check if a node's text (including trivia) contains a specific marker.
    bool hasMarker(const slang::syntax::SyntaxNode& node, std::string_view marker) const;

    /// Check if a node's leading trivia specifically contains a marker.
    /// This is more precise than hasMarker() as it only checks trivia, not content.
    bool hasMarkerInTrivia(const slang::syntax::SyntaxNode& node, std::string_view marker) const;

    /// Check if a token's trivia contains a specific marker.
    bool hasMarkerInTokenTrivia(slang::parsing::Token tok, std::string_view marker) const;

    /// Extract module type and instance name from an instance syntax node.
    std::optional<std::pair<std::string, std::string>>
    extractInstanceInfo(const slang::syntax::MemberSyntax& member) const;

    /// Extract declaration name from a member (wire/logic/reg).
    std::optional<std::string>
    extractDeclarationName(const slang::syntax::MemberSyntax& member) const;

    /// Find the template for a given module name.
    const AutoTemplate* findTemplate(const std::string& module_name) const;

    /// Generate AUTOINST port connection text.
    std::string generateAutoInstText(const AutoInstInfo& inst,
                                      const std::vector<PortInfo>& ports);

    /// Generate complete instance text with expanded port list (for replace).
    std::string generateFullInstanceText(const AutoInstInfo& inst,
                                          const std::vector<PortInfo>& ports,
                                          const std::string& indent);

    /// Generate AUTOWIRE declaration text.
    std::string generateAutowireText(const std::set<std::string>& existing_decls);

    /// Detect indentation from a node.
    std::string detectIndent(const slang::syntax::SyntaxNode& node) const;

    // ════════════════════════════════════════════════════════════════════════
    // Member data
    // ════════════════════════════════════════════════════════════════════════

    slang::ast::Compilation& compilation_;
    const std::vector<AutoTemplate>& templates_;
    AutosRewriterOptions options_;
    SignalAggregator aggregator_;
};

} // namespace slang_autos
