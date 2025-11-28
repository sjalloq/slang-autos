#pragma once

#include <set>
#include <string>
#include <vector>

#include <slang/syntax/SyntaxVisitor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/parsing/Token.h>

#include "Expander.h"  // For SignalAggregator, NetInfo

namespace slang_autos {

/// Rewriter that expands /*AUTOWIRE*/ comments using slang's SyntaxRewriter.
///
/// This uses the idiomatic slang approach:
/// - Comments are trivia attached to tokens, not syntax nodes
/// - Use parse() to convert generated Verilog text to syntax nodes
/// - Use insertAfter/remove to modify the syntax tree
class AutowireRewriter : public slang::syntax::SyntaxRewriter<AutowireRewriter> {
public:
    /// Construct a rewriter with signal information for wire generation.
    /// @param signals Aggregated signals from AUTOINST expansions
    /// @param existing_decls User-declared signal names to skip
    /// @param use_logic If true, use 'logic' instead of 'wire'
    AutowireRewriter(const SignalAggregator& signals,
                     const std::set<std::string>& existing_decls,
                     bool use_logic = true);

    /// Handle module declarations - this is where AUTOWIRE processing happens.
    void handle(const slang::syntax::ModuleDeclarationSyntax& module);

private:
    /// Check if a node's leading trivia contains /*AUTOWIRE*/
    bool hasAutowireMarker(const slang::syntax::SyntaxNode& node) const;

    /// Check if a node's leading trivia contains "// Beginning of automatic wires"
    bool isAutoBlockStart(const slang::syntax::SyntaxNode& node) const;

    /// Check if a node's leading trivia contains "// End of automatics"
    bool isAutoBlockEnd(const slang::syntax::SyntaxNode& node) const;

    /// Check trivia for a specific marker string
    bool hasMarkerInTrivia(const slang::syntax::SyntaxNode& node,
                           std::string_view marker) const;

    /// Generate wire declaration text (Verilog source)
    std::string generateDeclarations(std::string_view indent) const;

    const SignalAggregator& signals_;
    const std::set<std::string>& existing_decls_;
    bool use_logic_;
};

} // namespace slang_autos
