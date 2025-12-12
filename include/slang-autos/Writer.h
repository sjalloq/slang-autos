#pragma once

#include <filesystem>
#include <string>
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

private:
    bool dry_run_;
};

} // namespace slang_autos
