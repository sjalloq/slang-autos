#include "slang-autos/SignalAggregator.h"

#include <algorithm>

#include <slang/ast/Scope.h>
#include <slang/parsing/Parser.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>

namespace slang_autos {

// ============================================================================
// Helper Functions - Using slang AST for expression parsing
// ============================================================================

namespace {

/// Try to extract an integer value from a literal expression.
/// Returns -1 if the expression is not a simple integer literal.
int tryExtractIntegerLiteral(const slang::syntax::ExpressionSyntax& expr) {
    using namespace slang::syntax;
    if (expr.kind == SyntaxKind::IntegerLiteralExpression) {
        auto& lit = expr.as<LiteralExpressionSyntax>();
        auto text = std::string(lit.literal.rawText());
        try {
            return std::stoi(text);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

/// Extract the maximum bit index from element selects.
/// For [7] returns 7, for [63:32] returns 63.
/// Returns -1 if no valid bit index can be extracted.
int extractMaxBitFromSelectors(const slang::syntax::SyntaxList<slang::syntax::ElementSelectSyntax>& selectors) {
    using namespace slang::syntax;
    int max_bit = -1;

    for (auto* select : selectors) {
        if (!select->selector) continue;

        if (select->selector->kind == SyntaxKind::BitSelect) {
            // Single bit select like [7]
            auto& bitSel = select->selector->as<BitSelectSyntax>();
            int bit = tryExtractIntegerLiteral(*bitSel.expr);
            if (bit > max_bit) max_bit = bit;
        } else if (select->selector->kind == SyntaxKind::SimpleRangeSelect ||
                   select->selector->kind == SyntaxKind::AscendingRangeSelect ||
                   select->selector->kind == SyntaxKind::DescendingRangeSelect) {
            // Range select like [63:32] or [0:31] or [31:0]
            auto& rangeSel = select->selector->as<RangeSelectSyntax>();
            int left = tryExtractIntegerLiteral(*rangeSel.left);
            int right = tryExtractIntegerLiteral(*rangeSel.right);
            if (left > max_bit) max_bit = left;
            if (right > max_bit) max_bit = right;
        }
    }
    return max_bit;
}

/// Visitor that collects identifier names from a syntax tree.
/// Traverses all nodes and extracts names from identifier syntax nodes.
/// For member access (my_type.a) and scoped names (pkg::foo), only extracts
/// the root/base identifier, not the member or scoped part.
struct IdentifierCollector : public slang::syntax::SyntaxVisitor<IdentifierCollector> {
    std::vector<std::string> identifiers;
    int max_bit_index = -1;  // Track maximum bit index seen across all selects

    // Handle simple identifiers like "sig_a"
    void handle(const slang::syntax::IdentifierNameSyntax& node) {
        auto name = node.identifier.valueText();
        if (!name.empty()) {
            identifiers.emplace_back(name);
        }
    }

    // Handle identifiers with bit/part selects like "sig_a[7:0]"
    // IdentifierSelectNameSyntax contains an identifier token and element selects
    void handle(const slang::syntax::IdentifierSelectNameSyntax& node) {
        auto name = node.identifier.valueText();
        if (!name.empty()) {
            identifiers.emplace_back(name);
        }
        // Extract max bit index from selectors
        int bit = extractMaxBitFromSelectors(node.selectors);
        if (bit > max_bit_index) max_bit_index = bit;
        // Don't descend into selectors - we just want the base identifier
    }

    // Handle scoped names like "my_type.member" or "pkg::type"
    // Only extract identifiers from the left (base) side, not the right (member/scope)
    void handle(const slang::syntax::ScopedNameSyntax& node) {
        // Only visit the left side to get the root identifier
        node.left->visit(*this);
        // Don't visit node.right - that's the member/scope name, not a signal
    }

    // Handle member access expressions like "my_struct.field"
    // Only extract identifiers from the base expression, not the member name
    void handle(const slang::syntax::MemberAccessExpressionSyntax& node) {
        // Only visit the left side to get the base signal
        node.left->visit(*this);
        // Don't visit the member name token - it's not a separate signal
    }

    // Handle element select expressions like "signal[7]" as an expression
    void handle(const slang::syntax::ElementSelectExpressionSyntax& node) {
        // Visit the base expression to get identifiers
        node.left->visit(*this);
        // Extract max bit from this select
        if (node.select && node.select->selector) {
            auto& sel = *node.select;
            if (sel.selector->kind == slang::syntax::SyntaxKind::BitSelect) {
                auto& bitSel = sel.selector->as<slang::syntax::BitSelectSyntax>();
                int bit = tryExtractIntegerLiteral(*bitSel.expr);
                if (bit > max_bit_index) max_bit_index = bit;
            } else if (sel.selector->kind == slang::syntax::SyntaxKind::SimpleRangeSelect ||
                       sel.selector->kind == slang::syntax::SyntaxKind::AscendingRangeSelect ||
                       sel.selector->kind == slang::syntax::SyntaxKind::DescendingRangeSelect) {
                auto& rangeSel = sel.selector->as<slang::syntax::RangeSelectSyntax>();
                int left = tryExtractIntegerLiteral(*rangeSel.left);
                int right = tryExtractIntegerLiteral(*rangeSel.right);
                if (left > max_bit_index) max_bit_index = left;
                if (right > max_bit_index) max_bit_index = right;
            }
        }
    }
};

} // anonymous namespace

bool isVerilogConstant(const std::string& s) {
    if (s.empty()) return false;

    // Parse the expression using slang and check if it's a literal
    slang::BumpAllocator alloc;
    slang::Diagnostics diagnostics;
    slang::SourceManager sourceManager;

    slang::parsing::Preprocessor preprocessor(sourceManager, alloc, diagnostics);
    preprocessor.pushSource(s);

    slang::parsing::Parser parser(preprocessor);
    auto& expr = parser.parseExpression();

    // Check if the parsed expression is a literal
    // IntegerVectorExpression covers sized literals like 1'b0, 8'hFF
    // IntegerLiteralExpression covers plain decimal numbers like 42
    // UnbasedUnsizedLiteralExpression covers '0, '1, 'x, 'z
    using slang::syntax::SyntaxKind;
    switch (expr.kind) {
        case SyntaxKind::IntegerLiteralExpression:
        case SyntaxKind::IntegerVectorExpression:
        case SyntaxKind::RealLiteralExpression:
        case SyntaxKind::TimeLiteralExpression:
        case SyntaxKind::UnbasedUnsizedLiteralExpression:
        case SyntaxKind::NullLiteralExpression:
        case SyntaxKind::StringLiteralExpression:
        case SyntaxKind::WildcardLiteralExpression:
            return true;
        default:
            return false;
    }
}

std::vector<std::string> extractIdentifiersFromSyntax(const slang::syntax::SyntaxNode& node) {
    IdentifierCollector collector;
    node.visit(collector);
    return collector.identifiers;
}

std::vector<std::string> extractIdentifiers(const std::string& expr) {
    if (expr.empty()) return {};

    // Trim whitespace
    size_t start = expr.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return {};

    // Parse the expression using slang
    slang::BumpAllocator alloc;
    slang::Diagnostics diagnostics;
    slang::SourceManager sourceManager;

    slang::parsing::Preprocessor preprocessor(sourceManager, alloc, diagnostics);
    preprocessor.pushSource(expr);

    slang::parsing::Parser parser(preprocessor);
    auto& exprSyntax = parser.parseExpression();

    // Use the shared implementation
    return extractIdentifiersFromSyntax(exprSyntax);
}

int extractMaxBitIndex(const std::string& expr) {
    if (expr.empty()) return -1;

    // Parse the expression using slang
    slang::BumpAllocator alloc;
    slang::Diagnostics diagnostics;
    slang::SourceManager sourceManager;

    slang::parsing::Preprocessor preprocessor(sourceManager, alloc, diagnostics);
    preprocessor.pushSource(expr);

    slang::parsing::Parser parser(preprocessor);
    auto& exprSyntax = parser.parseExpression();

    // Visit the syntax tree to find max bit index
    IdentifierCollector collector;
    exprSyntax.visit(collector);
    return collector.max_bit_index;
}

bool isConcatenation(const std::string& expr) {
    if (expr.empty()) return false;

    // Parse the expression using slang
    slang::BumpAllocator alloc;
    slang::Diagnostics diagnostics;
    slang::SourceManager sourceManager;

    slang::parsing::Preprocessor preprocessor(sourceManager, alloc, diagnostics);
    preprocessor.pushSource(expr);

    slang::parsing::Parser parser(preprocessor);
    auto& exprSyntax = parser.parseExpression();

    // Check if the top-level expression is a concatenation
    return exprSyntax.kind == slang::syntax::SyntaxKind::ConcatenationExpression;
}

// ============================================================================
// SignalAggregator Implementation
// ============================================================================

void SignalAggregator::addFromInstance(
    const std::string& inst_name,
    const std::vector<PortConnection>& connections,
    const std::vector<PortInfo>& ports) {

    for (const auto& conn : connections) {
        // Skip unconnected and constant ports
        if (conn.is_unconnected || conn.is_constant) {
            continue;
        }

        // Find the port info to get width
        auto port_it = std::find_if(ports.begin(), ports.end(),
            [&](const PortInfo& p) { return p.name == conn.port_name; });

        if (port_it == ports.end()) {
            continue;
        }

        int port_width = port_it->width;
        // Prefer original syntax, fallback to resolved range
        std::string port_range = port_it->getRangeStr(true);

        // Extract max bit index from signal expression (e.g., signal[7] -> 7)
        // This handles cases where templates map multiple ports to different
        // bits of the same signal: data_in([0-9]) => data_bus[$1]
        int max_bit = extractMaxBitIndex(conn.signal_expr);
        int effective_width = port_width;
        std::string effective_range = port_range;

        if (max_bit >= 0) {
            // If bit index is specified, required width is max_bit + 1
            int required_width = max_bit + 1;
            if (required_width > effective_width) {
                effective_width = required_width;
                // Generate range string for the computed width
                effective_range = "[" + std::to_string(max_bit) + ":0]";
            }
        }

        // Use pre-extracted signal identifiers
        // These were populated at connection creation time
        if (conn.signal_identifiers.empty()) {
            continue;
        }

        // Process each extracted signal
        for (const auto& net_name : conn.signal_identifiers) {
            // Get or create net usage entry
            auto& usage = nets_[net_name];
            if (usage.info.name.empty()) {
                usage.info.name = net_name;
                usage.info.width = effective_width;
                usage.info.original_range_str = effective_range;
                if (effective_width > 1) {
                    usage.info.msb = effective_width - 1;
                    usage.info.lsb = 0;
                }
            } else {
                // Merge - take max width, keep original range from widest
                usage.info.merge(effective_width, effective_range);
            }

            // Track instance source
            if (std::find(usage.source_instances.begin(), usage.source_instances.end(), inst_name)
                == usage.source_instances.end()) {
                usage.source_instances.push_back(inst_name);
            }

            // Track direction usage
            if (conn.direction == "output") {
                usage.driven_by_instance = true;
            } else if (conn.direction == "input") {
                usage.consumed_by_instance = true;
            } else if (conn.direction == "inout") {
                usage.driven_by_instance = true;
                usage.consumed_by_instance = true;
                inout_nets_.insert(net_name);
            }

            // Mark signals from OUTPUT concatenations as internal-only
            // When an output port connects to {sig_a, sig_b}, those signals
            // are receiving parts of the output - they're internal wires.
            // But when an input port connects to {sig_a, sig_b}, those signals
            // are being combined as input - they should be ports.
            if (conn.is_concatenation && conn.direction == "output") {
                concatenation_nets_.insert(net_name);
            }
        }
    }
}

std::vector<NetInfo> SignalAggregator::getExternalInputNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // External input: consumed by instance input but NOT driven by any instance output
        // Exclude signals from concatenations (those are internal wires)
        if (usage.consumed_by_instance && !usage.driven_by_instance &&
            concatenation_nets_.find(name) == concatenation_nets_.end()) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getExternalOutputNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // External output: driven by instance output but NOT consumed by any instance input
        // Also exclude inouts and signals from concatenations (those are internal wires)
        if (usage.driven_by_instance && !usage.consumed_by_instance &&
            inout_nets_.find(name) == inout_nets_.end() &&
            concatenation_nets_.find(name) == concatenation_nets_.end()) {
            result.push_back(usage.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getInoutNets() const {
    std::vector<NetInfo> result;
    for (const auto& net_name : inout_nets_) {
        auto it = nets_.find(net_name);
        if (it != nets_.end()) {
            result.push_back(it->second.info);
        }
    }
    return result;
}

std::vector<NetInfo> SignalAggregator::getInternalNets() const {
    std::vector<NetInfo> result;
    for (const auto& [name, usage] : nets_) {
        // Internal net: driven by instance output AND consumed by instance input
        // Excludes inouts (those are external bidirectional ports)
        bool is_internal = usage.driven_by_instance && usage.consumed_by_instance &&
                          inout_nets_.find(name) == inout_nets_.end();

        // Also include signals from concatenations - these are internal wires
        // that need AUTOLOGIC declarations even if not both driven and consumed
        bool is_concat_signal = concatenation_nets_.find(name) != concatenation_nets_.end();

        if (is_internal || is_concat_signal) {
            result.push_back(usage.info);
        }
    }
    return result;
}

const NetInfo* SignalAggregator::getNetInfo(const std::string& name) const {
    auto it = nets_.find(name);
    if (it != nets_.end()) {
        return &it->second.info;
    }
    return nullptr;
}

void SignalAggregator::addUnusedSignal(const std::string& name, int width) {
    unused_signals_.emplace_back(name, width);
}

const std::vector<NetInfo>& SignalAggregator::getUnusedSignals() const {
    return unused_signals_;
}

} // namespace slang_autos
