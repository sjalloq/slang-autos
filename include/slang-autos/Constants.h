#pragma once

#include <string>
#include <string_view>

namespace slang_autos {

/// Centralized AUTO marker strings used throughout the codebase.
/// These constants ensure consistency and make the codebase easier to maintain.
///
/// Note: This tool focuses on SystemVerilog only. Legacy Verilog markers
/// (AUTOWIRE, AUTOREG) are not supported - use AUTOLOGIC and AUTOPORTS instead.
namespace markers {

// ============================================================================
// AUTO Comment Markers (SystemVerilog)
// ============================================================================

/// AUTOINST - expands instance port connections
inline constexpr std::string_view AUTOINST = "/*AUTOINST*/";

/// AUTOLOGIC - generates logic declarations for internal nets
inline constexpr std::string_view AUTOLOGIC = "/*AUTOLOGIC*/";

/// AUTOPORTS - generates ANSI-style port declarations
inline constexpr std::string_view AUTOPORTS = "/*AUTOPORTS*/";

/// AUTO_TEMPLATE - defines port mapping templates
inline constexpr std::string_view AUTO_TEMPLATE = "AUTO_TEMPLATE";

// ============================================================================
// Block Delimiter Comments
// ============================================================================

/// Beginning marker for automatic logic declarations
inline constexpr std::string_view BEGIN_AUTOLOGIC = "// Beginning of automatic logic";

/// End marker for all automatic declaration blocks
inline constexpr std::string_view END_AUTOMATICS = "// End of automatics";

} // namespace markers

// ============================================================================
// Maximum Values
// ============================================================================

/// Maximum indentation spaces allowed
inline constexpr int MAX_INDENT_SPACES = 16;

} // namespace slang_autos
