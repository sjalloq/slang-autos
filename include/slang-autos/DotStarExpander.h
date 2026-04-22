#pragma once

#include <memory>
#include <vector>

#include "Diagnostics.h"
#include "Writer.h"
#include "CompilationUtils.h"

// Forward declarations for slang types
namespace slang::ast {
class Compilation;
}
namespace slang::syntax {
class SyntaxTree;
}

namespace slang_autos {

/// Options for dot-star expansion
struct DotStarExpanderOptions {
    bool alignment = true;          ///< Align port connections
    StrictnessMode strictness = StrictnessMode::Lenient;
    int verbosity = 1;
};

/// Expands SystemVerilog .* (dot-star) wildcard port connections
/// into explicit .port_name(port_name) connections.
class DotStarExpander {
public:
    DotStarExpander(slang::ast::Compilation& compilation,
                    const DotStarExpanderOptions& opts = {});

    /// Walk a syntax tree and collect replacements for all .* connections.
    void analyze(const std::shared_ptr<slang::syntax::SyntaxTree>& tree,
                 std::string_view source_content);

    /// Get collected replacements (mutable for SourceWriter::applyReplacements)
    [[nodiscard]] std::vector<Replacement>& getReplacements() { return replacements_; }

    /// Number of .* wildcards expanded
    [[nodiscard]] int expandedCount() const { return expanded_count_; }

    /// Get diagnostics
    [[nodiscard]] DiagnosticCollector& diagnostics() { return diagnostics_; }

private:
    slang::ast::Compilation& compilation_;
    DotStarExpanderOptions options_;
    std::vector<Replacement> replacements_;
    DiagnosticCollector diagnostics_;
    int expanded_count_ = 0;
    std::string_view source_content_;
};

} // namespace slang_autos
