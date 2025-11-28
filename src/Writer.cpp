#include "slang-autos/Writer.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
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

// ============================================================================
// Helper Functions
// ============================================================================

std::optional<std::tuple<std::string, std::string, size_t>>
findInstanceInfoFromAutoinst(const std::string& content, size_t autoinst_start) {
    // Search backward for the instance header
    // Pattern: module_type [#(...)] instance_name (

    // First, find the opening paren of the port list
    size_t i = autoinst_start;
    if (i == 0) return std::nullopt;
    --i;

    int paren_depth = 0;
    bool found_open_paren = false;

    while (i > 0) {
        char c = content[i];

        if (c == ')') {
            ++paren_depth;
        } else if (c == '(') {
            if (paren_depth > 0) {
                --paren_depth;
            } else {
                // This is the opening paren of the port list
                found_open_paren = true;
                break;
            }
        }
        --i;
    }

    if (!found_open_paren) {
        return std::nullopt;
    }

    // Skip whitespace backward
    while (i > 0 && std::isspace(content[i - 1])) {
        --i;
    }

    if (i == 0) return std::nullopt;

    // Extract instance name
    size_t end = i;
    while (i > 0 && (std::isalnum(content[i - 1]) || content[i - 1] == '_' || content[i - 1] == '$')) {
        --i;
    }
    size_t start = i;
    std::string instance_name = content.substr(start, end - start);

    if (instance_name.empty()) {
        return std::nullopt;
    }

    // Skip whitespace and potential parameter list #(...)
    while (i > 0 && std::isspace(content[i - 1])) {
        --i;
    }

    if (i > 0 && content[i - 1] == ')') {
        // There's a parameter list - skip it
        --i;
        paren_depth = 1;
        while (i > 0 && paren_depth > 0) {
            --i;
            if (content[i] == ')') {
                ++paren_depth;
            } else if (content[i] == '(') {
                --paren_depth;
            }
        }

        // Skip the #
        while (i > 0 && std::isspace(content[i - 1])) {
            --i;
        }
        if (i > 0 && content[i - 1] == '#') {
            --i;
        }
        while (i > 0 && std::isspace(content[i - 1])) {
            --i;
        }
    }

    // Extract module type
    end = i;
    while (i > 0 && (std::isalnum(content[i - 1]) || content[i - 1] == '_' || content[i - 1] == '$')) {
        --i;
    }
    start = i;
    std::string module_type = content.substr(start, end - start);

    if (module_type.empty()) {
        return std::nullopt;
    }

    return std::make_tuple(module_type, instance_name, start);
}

std::optional<size_t>
findInstanceCloseParen(const std::string& content, size_t autoinst_end) {
    // Search forward for the closing paren, handling nesting
    int depth = 1;  // We're inside the instance's port list
    size_t i = autoinst_end;
    bool in_string = false;
    bool in_comment = false;
    char string_char = 0;

    while (i < content.length()) {
        char c = content[i];

        // Handle strings
        if (!in_comment) {
            if ((c == '"' || c == '\'') && !in_string) {
                in_string = true;
                string_char = c;
            } else if (c == string_char && in_string) {
                // Check for escape
                if (i > 0 && content[i - 1] != '\\') {
                    in_string = false;
                    string_char = 0;
                }
            }
        }

        // Handle comments
        if (!in_string) {
            if (c == '/' && i + 1 < content.length()) {
                char next_c = content[i + 1];
                if (next_c == '/' && !in_comment) {
                    // Line comment - skip to end of line
                    auto newline = content.find('\n', i);
                    i = (newline == std::string::npos) ? content.length() : newline;
                    continue;
                } else if (next_c == '*' && !in_comment) {
                    in_comment = true;
                    i += 2;
                    continue;
                }
            } else if (c == '*' && i + 1 < content.length() && content[i + 1] == '/') {
                if (in_comment) {
                    in_comment = false;
                    i += 2;
                    continue;
                }
            }
        }

        // Handle parens (only outside strings and comments)
        if (!in_string && !in_comment) {
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }

        ++i;
    }

    return std::nullopt;
}

std::set<std::string>
findManualPorts(const std::string& content, size_t autoinst_offset) {
    std::set<std::string> manual_ports;

    // Find the opening paren of the port list
    auto info = findInstanceInfoFromAutoinst(content, autoinst_offset);
    if (!info) {
        return manual_ports;
    }

    // Find where the port list starts
    size_t search_start = std::get<2>(*info);  // module type start
    auto open_paren = content.find('(', search_start);
    if (open_paren == std::string::npos || open_paren >= autoinst_offset) {
        return manual_ports;
    }

    // Extract the text between ( and /*AUTOINST*/
    std::string port_region = content.substr(open_paren + 1, autoinst_offset - open_paren - 1);

    // Find all .port_name( patterns
    static const std::regex port_re(R"(\.(\w+)\s*\()");
    std::sregex_iterator it(port_region.begin(), port_region.end(), port_re);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        manual_ports.insert((*it)[1].str());
    }

    return manual_ports;
}

std::set<std::string>
findExistingDeclarations(const std::string& content, size_t offset) {
    std::set<std::string> decls;

    // Find the module keyword before this offset
    std::string before = content.substr(0, offset);
    auto module_pos = before.rfind("module ");
    if (module_pos == std::string::npos) {
        return decls;
    }

    std::string search_region = content.substr(module_pos, offset - module_pos);

    // Pattern for declarations: wire/logic/reg/input/output/inout [range] name[, name...];
    static const std::regex decl_re(
        R"(\b(?:wire|logic|reg|input|output|inout)\b(?:\s*\[[^\]]+\])?\s*(\w+)(?:\s*,\s*(\w+))*\s*;)");

    std::sregex_iterator it(search_region.begin(), search_region.end(), decl_re);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        // First captured name
        if ((*it)[1].matched) {
            decls.insert((*it)[1].str());
        }
        // Additional comma-separated names
        for (size_t i = 2; i < it->size(); ++i) {
            if ((*it)[i].matched) {
                decls.insert((*it)[i].str());
            }
        }
    }

    return decls;
}

std::string detectIndent(const std::string& content, size_t offset) {
    // Find the start of the line
    size_t line_start = content.rfind('\n', offset);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;

    // Extract leading whitespace
    std::string indent;
    for (size_t i = line_start; i < content.length() && i < offset; ++i) {
        char c = content[i];
        if (c == ' ' || c == '\t') {
            indent += c;
        } else {
            break;
        }
    }

    return indent;
}

size_t offsetToLine(const std::string& content, size_t offset) {
    size_t line = 1;
    for (size_t i = 0; i < offset && i < content.length(); ++i) {
        if (content[i] == '\n') {
            ++line;
        }
    }
    return line;
}

size_t offsetToColumn(const std::string& content, size_t offset) {
    size_t last_newline = content.rfind('\n', offset);
    if (last_newline == std::string::npos) {
        return offset + 1;
    }
    return offset - last_newline;
}

} // namespace slang_autos
