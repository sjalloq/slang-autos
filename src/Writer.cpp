#include "slang-autos/Writer.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace slang_autos {

// ============================================================================
// SourceWriter Implementation
// ============================================================================

SourceWriter::SourceWriter(bool dry_run)
    : dry_run_(dry_run) {
}

std::string SourceWriter::applyReplacements(
    const std::string& content,
    std::vector<Replacement>& replacements) {

    // Sort by start offset, descending (apply from bottom up)
    std::sort(replacements.begin(), replacements.end(),
        [](const Replacement& a, const Replacement& b) {
            return a.start > b.start;
        });

    std::string result = content;

    for (const auto& repl : replacements) {
        result = result.substr(0, repl.start) +
                 repl.new_text +
                 result.substr(repl.end);
    }

    return result;
}

bool SourceWriter::writeFile(const std::filesystem::path& file, const std::string& content) {
    if (dry_run_) {
        return false;
    }

    std::ofstream ofs(file);
    if (!ofs) {
        return false;
    }

    ofs << content;
    return true;
}

std::string SourceWriter::generateDiff(
    const std::filesystem::path& file,
    const std::string& original,
    const std::string& modified) {

    // Simple unified diff implementation
    std::istringstream orig_stream(original);
    std::istringstream mod_stream(modified);

    std::vector<std::string> orig_lines, mod_lines;
    std::string line;

    while (std::getline(orig_stream, line)) {
        orig_lines.push_back(line);
    }
    while (std::getline(mod_stream, line)) {
        mod_lines.push_back(line);
    }

    std::ostringstream diff;
    diff << "--- a/" << file.string() << "\n";
    diff << "+++ b/" << file.string() << "\n";

    // Simple line-by-line comparison (not a true LCS diff)
    // For now, just show all changes
    size_t i = 0, j = 0;
    size_t context_lines = 3;

    while (i < orig_lines.size() || j < mod_lines.size()) {
        // Find next difference
        size_t match_start_i = i, match_start_j = j;

        while (i < orig_lines.size() && j < mod_lines.size() &&
               orig_lines[i] == mod_lines[j]) {
            ++i;
            ++j;
        }

        if (i >= orig_lines.size() && j >= mod_lines.size()) {
            break;
        }

        // Found a difference - output hunk header
        size_t hunk_start_i = (match_start_i > context_lines) ? match_start_i - context_lines : 0;
        size_t hunk_start_j = (match_start_j > context_lines) ? match_start_j - context_lines : 0;

        // Find end of difference
        size_t diff_end_i = i, diff_end_j = j;
        while (diff_end_i < orig_lines.size() || diff_end_j < mod_lines.size()) {
            if (diff_end_i < orig_lines.size() && diff_end_j < mod_lines.size() &&
                orig_lines[diff_end_i] == mod_lines[diff_end_j]) {
                // Found matching lines - check if we have enough context
                size_t match_count = 0;
                while (diff_end_i + match_count < orig_lines.size() &&
                       diff_end_j + match_count < mod_lines.size() &&
                       orig_lines[diff_end_i + match_count] == mod_lines[diff_end_j + match_count]) {
                    ++match_count;
                    if (match_count >= context_lines * 2) {
                        break;
                    }
                }
                if (match_count >= context_lines * 2) {
                    diff_end_i += context_lines;
                    diff_end_j += context_lines;
                    break;
                }
            }
            if (diff_end_i < orig_lines.size()) ++diff_end_i;
            if (diff_end_j < mod_lines.size()) ++diff_end_j;
        }

        // Output hunk
        diff << "@@ -" << (hunk_start_i + 1) << "," << (diff_end_i - hunk_start_i)
             << " +" << (hunk_start_j + 1) << "," << (diff_end_j - hunk_start_j) << " @@\n";

        // Output context before
        for (size_t k = hunk_start_i; k < match_start_i && k < orig_lines.size(); ++k) {
            diff << " " << orig_lines[k] << "\n";
        }

        // Output changes
        for (size_t k = match_start_i; k < i && k < orig_lines.size(); ++k) {
            diff << " " << orig_lines[k] << "\n";
        }

        // Removed lines
        while (i < diff_end_i && i < orig_lines.size() &&
               (j >= mod_lines.size() || orig_lines[i] != mod_lines[j])) {
            diff << "-" << orig_lines[i] << "\n";
            ++i;
        }

        // Added lines
        while (j < diff_end_j && j < mod_lines.size() &&
               (i >= orig_lines.size() || orig_lines[i] != mod_lines[j])) {
            diff << "+" << mod_lines[j] << "\n";
            ++j;
        }

        // Context after
        for (size_t k = 0; k < context_lines && i < orig_lines.size() && j < mod_lines.size(); ++k) {
            if (orig_lines[i] == mod_lines[j]) {
                diff << " " << orig_lines[i] << "\n";
                ++i;
                ++j;
            } else {
                break;
            }
        }
    }

    return diff.str();
}

} // namespace slang_autos
