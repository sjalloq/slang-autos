#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace slang_autos {

/// A text replacement to apply to source content.
struct Replacement {
    size_t start;           ///< Start byte offset (inclusive)
    size_t end;             ///< End byte offset (exclusive)
    std::string new_text;   ///< Replacement text
    std::string description;///< Optional description for logging

    Replacement() : start(0), end(0) {}
    Replacement(size_t s, size_t e, std::string text, std::string desc = "")
        : start(s), end(e), new_text(std::move(text)), description(std::move(desc)) {}
};

/// Handles in-place modification of source files.
/// Applies replacements bottom-up (highest offset first) to preserve earlier offsets.
class SourceWriter {
public:
    explicit SourceWriter(bool dry_run = false);

    /// Apply replacements to text content.
    /// Replacements are sorted by offset and applied from end to start.
    /// @param content Original text content
    /// @param replacements List of replacements (will be sorted)
    /// @return Modified text content
    [[nodiscard]] std::string applyReplacements(
        const std::string& content,
        std::vector<Replacement>& replacements);

    /// Write content to a file.
    /// @param file Path to write to
    /// @param content Content to write
    /// @return true if file was written (false if dry_run)
    bool writeFile(const std::filesystem::path& file, const std::string& content);

    /// Generate a unified diff between original and modified content.
    /// @param file File path for diff header
    /// @param original Original content
    /// @param modified Modified content
    /// @return Unified diff string
    [[nodiscard]] std::string generateDiff(
        const std::filesystem::path& file,
        const std::string& original,
        const std::string& modified);

    /// Check if in dry-run mode
    [[nodiscard]] bool isDryRun() const { return dry_run_; }

private:
    bool dry_run_;
};

// ============================================================================
// Helper Functions
// ============================================================================

/// Find instance info by searching backward from AUTOINST comment.
/// Extracts module_type, instance_name, and start offset.
/// @param content Source file content
/// @param autoinst_start Byte offset of /*AUTOINST*/
/// @return Tuple of (module_type, instance_name, instance_start_offset) or nullopt
[[nodiscard]] std::optional<std::tuple<std::string, std::string, size_t>>
findInstanceInfoFromAutoinst(const std::string& content, size_t autoinst_start);

/// Find the closing parenthesis of the instance containing AUTOINST.
/// Handles nested parens, strings, and comments.
/// @param content Source file content
/// @param autoinst_end Byte offset just after /*AUTOINST*/
/// @return Byte offset of the closing ')' or nullopt if not found
[[nodiscard]] std::optional<size_t>
findInstanceCloseParen(const std::string& content, size_t autoinst_end);

/// Extract manually connected ports from the instance text before AUTOINST.
/// Finds patterns like .port_name( in the port list.
/// @param content Source file content
/// @param autoinst_offset Byte offset of AUTOINST comment
/// @return Set of port names that are manually connected
[[nodiscard]] std::set<std::string>
findManualPorts(const std::string& content, size_t autoinst_offset);

/// Find existing signal declarations before a given offset.
/// Looks for wire/logic/reg/input/output/inout declarations.
/// @param content Source file content
/// @param offset Search backward from this offset
/// @return Set of declared signal names
[[nodiscard]] std::set<std::string>
findExistingDeclarations(const std::string& content, size_t offset);

/// Detect the indentation style at a given offset.
/// @param content Source file content
/// @param offset Byte offset (searches backward to line start)
/// @return Indentation string (spaces/tabs at start of line)
[[nodiscard]] std::string
detectIndent(const std::string& content, size_t offset);

/// Calculate line number from byte offset.
/// @param content Source file content
/// @param offset Byte offset
/// @return 1-based line number
[[nodiscard]] size_t
offsetToLine(const std::string& content, size_t offset);

/// Calculate column number from byte offset.
/// @param content Source file content
/// @param offset Byte offset
/// @return 1-based column number
[[nodiscard]] size_t
offsetToColumn(const std::string& content, size_t offset);

// ============================================================================
// Enhanced Declaration Scanning
// ============================================================================

/// Information about a declared signal.
struct DeclaredSignal {
    std::string name;       ///< Signal name
    std::string type;       ///< Declaration type: "wire", "logic", "reg", "input", "output", "inout"
    int width = 1;          ///< Bit width (1 for scalar)

    DeclaredSignal() = default;
    DeclaredSignal(std::string n, std::string t, int w = 1)
        : name(std::move(n)), type(std::move(t)), width(w) {}
};

/// Find AUTO block ranges in content (to exclude from declaration scanning).
/// Returns pairs of (start, end) byte offsets for regions that should be excluded.
/// This includes:
/// - "// Beginning of automatic wires" ... "// End of automatics"
/// - Content between /*AUTOPORTS*/ and closing )
/// @param content Source file content
/// @return Vector of (start, end) offset pairs for AUTO blocks
[[nodiscard]] std::vector<std::pair<size_t, size_t>>
findAutoBlockRanges(const std::string& content);

/// Find all user-declared signals in a module, excluding AUTO-generated blocks.
/// @param content Source file content
/// @param module_start Byte offset of module start
/// @param module_end Byte offset of module end (or endmodule)
/// @param exclude_ranges Ranges to exclude (from findAutoBlockRanges)
/// @return Vector of declared signals
[[nodiscard]] std::vector<DeclaredSignal>
findModuleDeclarations(
    const std::string& content,
    size_t module_start,
    size_t module_end,
    const std::vector<std::pair<size_t, size_t>>& exclude_ranges);

/// Check if an offset is within any of the excluded ranges.
[[nodiscard]] bool
isInExcludedRange(size_t offset, const std::vector<std::pair<size_t, size_t>>& ranges);

/// Find the module boundary (module ... endmodule) containing an offset.
/// @param content Source file content
/// @param offset Byte offset within the module
/// @return Pair of (module_start, module_end) or nullopt if not found
[[nodiscard]] std::optional<std::pair<size_t, size_t>>
findModuleBoundary(const std::string& content, size_t offset);

} // namespace slang_autos
