#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Diagnostics.h"

// Forward declarations for slang types
namespace slang::ast {
class Compilation;
}

namespace slang_autos {

/// Port information extracted from module definitions.
/// Contains both resolved values (for connection) and original syntax (for declarations).
struct PortInfo {
    std::string name;               ///< Port name
    std::string direction;          ///< "input", "output", "inout"
    std::string type_str = "logic"; ///< Type: "wire", "logic", "reg", etc.
    int width = 1;                  ///< Resolved bit width
    std::string range_str;          ///< Resolved range: "[7:0]"
    std::string original_range_str; ///< Original syntax: "[WIDTH-1:0]"
    std::optional<int> msb;         ///< Most significant bit (if range)
    std::optional<int> lsb;         ///< Least significant bit (if range)
    bool is_signed = false;
    bool is_array = false;
    std::string array_dims;         ///< Array dimensions if any

    PortInfo() = default;
    PortInfo(std::string n, std::string dir, int w = 1)
        : name(std::move(n)), direction(std::move(dir)), width(w) {}

    /// Get the range string, optionally preferring original syntax
    [[nodiscard]] std::string getRangeStr(bool prefer_original = true) const {
        if (prefer_original && !original_range_str.empty()) {
            return original_range_str;
        }
        return range_str;
    }
};

/// Extract port information for a module from a slang compilation.
/// Searches top instances for submodule instantiations matching the given name.
/// @param compilation The slang compilation containing parsed design (non-const due to lazy eval)
/// @param module_name Name of the module to look up
/// @param diagnostics Optional diagnostic collector for errors/warnings
/// @param strictness Error vs warning for missing modules
/// @return Vector of port information (empty if module not found)
[[nodiscard]] std::vector<PortInfo> getModulePortsFromCompilation(
    slang::ast::Compilation& compilation,
    const std::string& module_name,
    DiagnosticCollector* diagnostics = nullptr,
    StrictnessMode strictness = StrictnessMode::Lenient);

} // namespace slang_autos
