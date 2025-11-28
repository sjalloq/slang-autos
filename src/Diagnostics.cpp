#include "slang-autos/Diagnostics.h"

#include <sstream>

namespace slang_autos {

void DiagnosticCollector::addWarning(const std::string& msg,
                                     const std::string& file,
                                     size_t line,
                                     const std::string& type) {
    diagnostics_.emplace_back(DiagnosticLevel::Warning, msg, file, line, type);
    ++warning_count_;
}

void DiagnosticCollector::addError(const std::string& msg,
                                   const std::string& file,
                                   size_t line,
                                   const std::string& type) {
    diagnostics_.emplace_back(DiagnosticLevel::Error, msg, file, line, type);
    ++error_count_;
}

void DiagnosticCollector::clear() {
    diagnostics_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

std::string DiagnosticCollector::format() const {
    std::ostringstream oss;

    for (const auto& diag : diagnostics_) {
        // Format: file:line: level: message
        if (!diag.file_path.empty()) {
            oss << diag.file_path;
            if (diag.line_number > 0) {
                oss << ":" << diag.line_number;
            }
            oss << ": ";
        }

        oss << (diag.level == DiagnosticLevel::Error ? "error" : "warning");
        oss << ": " << diag.message << "\n";
    }

    return oss.str();
}

} // namespace slang_autos
