// Integration tests for slang-autos
// These tests exercise the full slang driver flow with real SystemVerilog files

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "slang-autos/Tool.h"

namespace fs = std::filesystem;
using namespace slang_autos;

// Helper to get path to test fixtures
static fs::path getFixturePath(const std::string& relative) {
    // Find fixtures relative to test executable or source
    fs::path candidates[] = {
        fs::path(__FILE__).parent_path() / "fixtures" / relative,
        fs::current_path() / "tests" / "fixtures" / relative,
        fs::current_path() / "fixtures" / relative,
    };

    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            return path;
        }
    }

    // Return first candidate for error message
    return candidates[0];
}

// Helper to read file content
static std::string readFile(const fs::path& path) {
    std::ifstream ifs(path);
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

// =============================================================================
// Library Resolution Tests (would have caught parseAllSources bug)
// =============================================================================

TEST_CASE("Integration - library module resolution via -y", "[integration]") {
    auto top_sv = getFixturePath("simple/top.sv");
    auto lib_dir = getFixturePath("simple/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 1);

    // Verify port connections were generated
    CHECK(result.modified_content.find(".clk") != std::string::npos);
    CHECK(result.modified_content.find(".rst_n") != std::string::npos);
    CHECK(result.modified_content.find(".data_in") != std::string::npos);
    CHECK(result.modified_content.find(".data_out") != std::string::npos);
}

TEST_CASE("Integration - library resolution with +libext+", "[integration]") {
    auto top_sv = getFixturePath("simple/top.sv");
    auto lib_dir = getFixturePath("simple/lib");

    REQUIRE(fs::exists(top_sv));

    // Test with multiple extensions
    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.v+.sv+.vh"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);
    CHECK(result.success);
    CHECK(result.autoinst_count == 1);
}

// =============================================================================
// Module Not Found Tests (would have caught content wiping bug)
// =============================================================================

TEST_CASE("Integration - module not found preserves content", "[integration]") {
    auto missing_sv = getFixturePath("errors/missing_module.sv");

    REQUIRE(fs::exists(missing_sv));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({missing_sv.string()});

    REQUIRE(loaded);

    auto result = tool.expandFile(missing_sv, true);

    // Original content should be preserved
    CHECK(result.original_content == result.modified_content);
    CHECK(result.autoinst_count == 0);

    // Warning should be issued
    CHECK(tool.diagnostics().warningCount() > 0);
}

TEST_CASE("Integration - strict mode errors on missing module", "[integration]") {
    auto missing_sv = getFixturePath("errors/missing_module.sv");

    REQUIRE(fs::exists(missing_sv));

    AutosTool::Options opts;
    opts.strictness = StrictnessMode::Strict;

    AutosTool tool(opts);
    bool loaded = tool.loadWithArgs({missing_sv.string()});

    REQUIRE(loaded);

    auto result = tool.expandFile(missing_sv, true);

    // In strict mode, missing module should produce error
    CHECK(tool.diagnostics().hasErrors());
}

TEST_CASE("Integration - lenient mode warns on missing module", "[integration]") {
    auto missing_sv = getFixturePath("errors/missing_module.sv");

    REQUIRE(fs::exists(missing_sv));

    AutosTool::Options opts;
    opts.strictness = StrictnessMode::Lenient;

    AutosTool tool(opts);
    bool loaded = tool.loadWithArgs({missing_sv.string()});

    REQUIRE(loaded);

    auto result = tool.expandFile(missing_sv, true);

    // In lenient mode, missing module should only warn
    CHECK_FALSE(tool.diagnostics().hasErrors());
    CHECK(tool.diagnostics().warningCount() > 0);
}

// =============================================================================
// Template Tests (end-to-end template functionality)
// =============================================================================

TEST_CASE("Integration - template with @ substitution", "[integration][templates]") {
    auto top_sv = getFixturePath("templates/top.sv");
    auto lib_dir = getFixturePath("templates/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 2);

    // Verify @ substitution worked (u_fifo_0 -> 0, u_fifo_1 -> 1)
    CHECK(result.modified_content.find("data_0_in") != std::string::npos);
    CHECK(result.modified_content.find("data_0_out") != std::string::npos);
    CHECK(result.modified_content.find("data_1_in") != std::string::npos);
    CHECK(result.modified_content.find("data_1_out") != std::string::npos);
}

TEST_CASE("Integration - multiple templates for same module uses closest preceding", "[integration][templates]") {
    // This tests verilog-mode semantics: each instance should use the closest
    // preceding template for that module, not the first one in the file.
    auto top_sv = getFixturePath("multi_template/top.sv");
    auto lib_dir = getFixturePath("multi_template/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 2);

    // First instance (u_sub_a) should use first template (sig_a_ prefix)
    CHECK(result.modified_content.find("sig_a_clk") != std::string::npos);
    CHECK(result.modified_content.find("sig_a_data_in") != std::string::npos);
    CHECK(result.modified_content.find("sig_a_data_out") != std::string::npos);

    // Second instance (u_sub_b) should use second template (sig_b_ prefix)
    CHECK(result.modified_content.find("sig_b_clk") != std::string::npos);
    CHECK(result.modified_content.find("sig_b_data_in") != std::string::npos);
    CHECK(result.modified_content.find("sig_b_data_out") != std::string::npos);

    // Verify the signals are NOT incorrectly mixed (bug would cause both to use sig_a_)
    // Count occurrences to ensure each prefix appears exactly once per signal
    auto count = [](const std::string& haystack, const std::string& needle) {
        size_t count = 0;
        size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.length();
        }
        return count;
    };

    // Each signal name should appear exactly once in the output
    CHECK(count(result.modified_content, "(sig_a_clk)") == 1);
    CHECK(count(result.modified_content, "(sig_b_clk)") == 1);
}

TEST_CASE("Integration - manual port without trailing comma", "[integration]") {
    // Test that a comma is automatically added between manual ports and auto ports
    // when the user doesn't include a trailing comma
    auto top_sv = getFixturePath("manual_ports/top_no_comma.sv");
    auto lib_dir = getFixturePath("manual_ports/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 1);

    // Manual port (clk) should not be duplicated
    auto count = [](const std::string& haystack, const std::string& needle) {
        size_t count = 0;
        size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.length();
        }
        return count;
    };

    CHECK(count(result.modified_content, ".clk") == 1);  // Only manual, not auto

    // Auto ports should be present
    CHECK(result.modified_content.find(".rst_n") != std::string::npos);
    CHECK(result.modified_content.find(".data_in") != std::string::npos);
    CHECK(result.modified_content.find(".data_out") != std::string::npos);

    // Output should be valid SystemVerilog (comma between .clk and auto ports)
    // The comma is added after /*AUTOINST*/
    CHECK(result.modified_content.find("/*AUTOINST*/,") != std::string::npos);
}

TEST_CASE("Integration - parameterized port widths preserve original syntax", "[integration]") {
    // AUTOLOGIC should preserve original syntax (e.g., [WIDTH-1:0])
    // The user is responsible for ensuring parameters are in scope
    auto top_sv = getFixturePath("param_width/top_autologic.sv");
    auto lib_dir = getFixturePath("param_width/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autologic_count == 1);

    // internal_data should be declared with original syntax [WIDTH-1:0]
    // Note: User must ensure WIDTH is in scope for valid SystemVerilog
    CHECK(result.modified_content.find("logic  [WIDTH-1:0] internal_data") != std::string::npos);
}

TEST_CASE("Integration - macro port widths preserve original syntax", "[integration]") {
    // AUTOPORTS should preserve original macro syntax (e.g., [`DATA_WIDTH-1:0])
    // not the expanded value (e.g., [8-1:0] or [7:0])
    auto top_sv = getFixturePath("macro_test/top.sv");
    auto lib_dir = getFixturePath("macro_test/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 1);

    // Ports should use original macro syntax, not expanded values
    CHECK(result.modified_content.find("`DATA_WIDTH-1:0]") != std::string::npos);
    // Should NOT have the expanded form
    CHECK(result.modified_content.find("[8-1:0]") == std::string::npos);
    CHECK(result.modified_content.find("[7:0]") == std::string::npos);
}

TEST_CASE("Integration - resolved_ranges option outputs resolved widths", "[integration]") {
    // With resolved_ranges option, should output [7:0] instead of [`DATA_WIDTH-1:0]
    auto top_sv = getFixturePath("macro_test/top.sv");
    auto lib_dir = getFixturePath("macro_test/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool::Options opts;
    opts.resolved_ranges = true;
    AutosTool tool(opts);

    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 1);

    // With resolved_ranges, should have resolved widths
    CHECK(result.modified_content.find("[7:0]") != std::string::npos);
    // Should NOT have macro syntax
    CHECK(result.modified_content.find("`DATA_WIDTH") == std::string::npos);
}

TEST_CASE("Integration - shared port declarations (input a, b, c)", "[integration]") {
    // Test that ports declared together like "input a, b, c" are all expanded correctly
    auto top_sv = getFixturePath("multiport/top.sv");
    auto lib_dir = getFixturePath("multiport/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 1);

    // All 6 ports should be present (clk, rst_n from "input clk, rst_n",
    // a, b, c from "input [7:0] a, b, c", out from "output [7:0] out")
    CHECK(result.modified_content.find(".clk") != std::string::npos);
    CHECK(result.modified_content.find(".rst_n") != std::string::npos);
    CHECK(result.modified_content.find(".a") != std::string::npos);
    CHECK(result.modified_content.find(".b") != std::string::npos);
    CHECK(result.modified_content.find(".c") != std::string::npos);
    CHECK(result.modified_content.find(".out") != std::string::npos);
}

// =============================================================================
// EDA Argument Tests (would have caught +libext+ as file bug)
// =============================================================================

TEST_CASE("Integration - +incdir+ works", "[integration]") {
    auto top_sv = getFixturePath("simple/top.sv");
    auto lib_dir = getFixturePath("simple/lib");

    REQUIRE(fs::exists(top_sv));

    // +incdir+ should be accepted without error
    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv",
        "+incdir+" + lib_dir.string()
    });

    REQUIRE(loaded);
}

TEST_CASE("Integration - +define+ works", "[integration]") {
    auto top_sv = getFixturePath("simple/top.sv");
    auto lib_dir = getFixturePath("simple/lib");

    REQUIRE(fs::exists(top_sv));

    // +define+ should be accepted without error
    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv",
        "+define+WIDTH=8"
    });

    REQUIRE(loaded);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_CASE("Integration - nonexistent file fails gracefully", "[integration][errors]") {
    AutosTool tool;
    bool loaded = tool.loadWithArgs({"nonexistent_file.sv"});

    // Should fail to load
    CHECK_FALSE(loaded);
    CHECK(tool.diagnostics().hasErrors());
}

TEST_CASE("Integration - nonexistent -f file fails", "[integration][errors]") {
    AutosTool tool;
    bool loaded = tool.loadWithArgs({"-f", "nonexistent.f"});

    CHECK_FALSE(loaded);
    CHECK(tool.diagnostics().hasErrors());
}

TEST_CASE("Integration - empty args fails gracefully", "[integration][errors]") {
    AutosTool tool;
    bool loaded = tool.loadWithArgs({});

    // Empty args should fail
    CHECK_FALSE(loaded);
}

// =============================================================================
// Dry Run Tests
// =============================================================================

TEST_CASE("Integration - dry run does not modify content", "[integration]") {
    auto top_sv = getFixturePath("simple/top.sv");
    auto lib_dir = getFixturePath("simple/lib");

    REQUIRE(fs::exists(top_sv));

    // Read original content
    std::string original = readFile(top_sv);

    AutosTool tool;
    tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    // Expand with dry_run=true
    auto result = tool.expandFile(top_sv, /*dry_run=*/true);

    CHECK(result.success);
    CHECK(result.hasChanges());

    // File should still have original content
    std::string after = readFile(top_sv);
    CHECK(original == after);
}

// =============================================================================
// Multiple Instance Tests
// =============================================================================

TEST_CASE("Integration - multiple instances of same module", "[integration]") {
    // Create a temp file with multiple instances
    auto temp_dir = fs::temp_directory_path() / "slang_autos_test";
    fs::create_directories(temp_dir / "lib");

    // Write top module
    auto top_path = temp_dir / "top.sv";
    {
        std::ofstream ofs(top_path);
        ofs << "module top;\n"
            << "    submod u_sub0 (/*AUTOINST*/);\n"
            << "    submod u_sub1 (/*AUTOINST*/);\n"
            << "    submod u_sub2 (/*AUTOINST*/);\n"
            << "endmodule\n";
    }

    // Write submodule
    auto sub_path = temp_dir / "lib" / "submod.sv";
    {
        std::ofstream ofs(sub_path);
        ofs << "module submod(\n"
            << "    input wire clk,\n"
            << "    input wire rst_n\n"
            << ");\n"
            << "endmodule\n";
    }

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_path.string(),
        "-y", (temp_dir / "lib").string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_path, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 3);

    // Cleanup
    fs::remove_all(temp_dir);
}

// =============================================================================
// CLI Behavior Tests (run actual binary)
// =============================================================================

// =============================================================================
// Multi-Instance Comprehensive Test (AUTOLOGIC + AUTOPORTS)
// =============================================================================

TEST_CASE("Multi-instance - signal flow and classification", "[integration][autologic][autoports]") {
    auto top_sv = getFixturePath("multi_instance/top.sv");
    auto lib_dir = getFixturePath("multi_instance/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    CHECK(result.success);
    CHECK(result.autoinst_count == 2);

    // This fixture tests:
    // - clk, rst_n: consumed by both instances, not driven -> EXTERNAL INPUTS
    // - data, valid: driven by producer, consumed by consumer -> INTERNAL LOGIC
    // - result, result_valid: driven by consumer, not consumed -> EXTERNAL OUTPUTS
    // - Width conflict: producer outputs 16-bit data, consumer inputs 8-bit -> max (16-bit)
    // - User-declared signal: user_declared_wire should be skipped

    // === AUTOLOGIC section checks ===
    auto autologic_start = result.modified_content.find("// Beginning of automatic logic");
    REQUIRE(autologic_start != std::string::npos);

    auto autologic_end = result.modified_content.find("// End of automatics", autologic_start);
    REQUIRE(autologic_end != std::string::npos);

    auto autologic_section = result.modified_content.substr(autologic_start, autologic_end - autologic_start);

    // Internal nets SHOULD be in AUTOLOGIC
    CHECK(autologic_section.find("data") != std::string::npos);
    CHECK(autologic_section.find("valid") != std::string::npos);

    // Width conflict: data should be 16-bit (max of 16 and 8)
    CHECK(autologic_section.find("[15:0]") != std::string::npos);

    // External signals should NOT be in AUTOLOGIC
    CHECK(autologic_section.find("clk") == std::string::npos);
    CHECK(autologic_section.find("rst_n") == std::string::npos);
    CHECK(autologic_section.find("result") == std::string::npos);
    CHECK(autologic_section.find("result_valid") == std::string::npos);

    // User-declared signal should NOT be in AUTOLOGIC
    CHECK(autologic_section.find("user_declared_wire") == std::string::npos);

    // === AUTOPORTS section checks ===
    auto autoports_start = result.modified_content.find("/*AUTOPORTS*/");
    REQUIRE(autoports_start != std::string::npos);

    auto ports_end = result.modified_content.find(");", autoports_start);
    REQUIRE(ports_end != std::string::npos);

    auto ports_section = result.modified_content.substr(autoports_start, ports_end - autoports_start);

    // External inputs SHOULD be in AUTOPORTS
    CHECK(ports_section.find("clk") != std::string::npos);
    CHECK(ports_section.find("rst_n") != std::string::npos);
    CHECK(ports_section.find("input") != std::string::npos);

    // External outputs SHOULD be in AUTOPORTS
    CHECK(ports_section.find("result") != std::string::npos);
    CHECK(ports_section.find("result_valid") != std::string::npos);
    CHECK(ports_section.find("output") != std::string::npos);

    // Internal wires should NOT be in AUTOPORTS
    // Note: 'data' appears in 'data' but also in 'result_valid', need to be specific
    // Check that 'data' without result prefix is not a port
    bool data_as_port = ports_section.find("logic") != std::string::npos &&
                        (ports_section.find(" data,") != std::string::npos ||
                         ports_section.find(" data\n") != std::string::npos);
    CHECK_FALSE(data_as_port);
}

// =============================================================================
// Indentation Preservation Tests
// =============================================================================

TEST_CASE("Integration - indentation preservation", "[integration][formatting]") {
    auto top_sv = getFixturePath("indentation/top.sv");
    auto lib_dir = getFixturePath("indentation/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, true);

    CHECK(result.success);
    CHECK(result.autoinst_count == 1);

    // Port connections should be at base_indent + one_level
    // Fixture uses 2-space indent, so:
    // - Instance line: 2 spaces
    // - Port connections: 4 spaces (2 + 2)
    // - Closing );: 2 spaces
    CHECK(result.modified_content.find("\n    .clk") != std::string::npos);  // 4 spaces
    CHECK(result.modified_content.find("\n  );") != std::string::npos);       // 2 spaces
}

// =============================================================================
// AUTOPORTS Tests
// =============================================================================

TEST_CASE("AUTOPORTS - basic port generation", "[integration][autoports]") {
    auto top_sv = getFixturePath("autoports_basic/top.sv");
    auto lib_dir = getFixturePath("autoports_basic/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);

    CHECK(result.success);

    // AUTOPORTS should generate ports for external signals:
    // - data_in is an external input (consumed by submod but not driven)
    // - data_out is an external output (driven by submod but not consumed)
    // - valid is an external output (driven by submod but not consumed)
    // clk and rst_n are already declared manually

    // Check that the marker is preserved
    CHECK(result.modified_content.find("/*AUTOPORTS*/") != std::string::npos);

    // Check for generated output ports
    CHECK(result.modified_content.find("output") != std::string::npos);
    CHECK(result.modified_content.find("data_out") != std::string::npos);
    CHECK(result.modified_content.find("valid") != std::string::npos);

    // Check for generated input port
    CHECK(result.modified_content.find("data_in") != std::string::npos);
}

TEST_CASE("AUTOPORTS - preserves user-defined ports", "[integration][autoports]") {
    // This tests that user-defined ports before /*AUTOPORTS*/ are preserved.
    // Also tests the case where a signal is connected between submodule I/Os
    // (would be classified as "internal net" by aggregator) but is declared
    // as a user port and should remain a port, not become a wire.
    auto top_sv = getFixturePath("autoports_user_defined/top.sv");
    auto lib_dir = getFixturePath("autoports_user_defined/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });

    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);

    CHECK(result.success);

    // User-defined port should be preserved (not overwritten)
    // Note: some_sig is connected between producer output and consumer input,
    // so the aggregator would classify it as "internal", but since user declared
    // it as a port, it should remain a port.
    CHECK(result.modified_content.find("output logic [2:0] some_sig") != std::string::npos);

    // The marker should still be present
    CHECK(result.modified_content.find("/*AUTOPORTS*/") != std::string::npos);

    // Auto-generated ports should also be present
    CHECK(result.modified_content.find("data_out") != std::string::npos);
    CHECK(result.modified_content.find("data_in") != std::string::npos);
    CHECK(result.modified_content.find("clk") != std::string::npos);
    CHECK(result.modified_content.find("rst_n") != std::string::npos);

    // AUTOWIRE should NOT generate a wire for some_sig (it's a user port)
    // Count occurrences of some_sig - it should only appear in port declaration
    // and AUTOINST connections, not in AUTOWIRE
    size_t autowire_pos = result.modified_content.find("/*AUTOWIRE*/");
    if (autowire_pos != std::string::npos) {
        // Check that there's no wire declaration for some_sig after AUTOWIRE
        size_t some_sig_after_autowire = result.modified_content.find("wire", autowire_pos);
        // If there's no wire section at all, or some_sig isn't in it, that's correct
        if (some_sig_after_autowire != std::string::npos) {
            size_t end_marker = result.modified_content.find("// End of automatics", autowire_pos);
            if (end_marker != std::string::npos) {
                std::string wire_section = result.modified_content.substr(autowire_pos, end_marker - autowire_pos);
                // some_sig should NOT be declared as a wire
                CHECK(wire_section.find("some_sig") == std::string::npos);
            }
        }
    }
}

// =============================================================================
// CLI Behavior Tests (run actual binary)
// =============================================================================

TEST_CASE("CLI - only positional files are expanded, not -f files", "[cli]") {
    // This tests that files from -f provide compilation context but are NOT expanded.
    // Only explicitly named positional files should be expanded.

    auto temp_dir = fs::temp_directory_path() / "slang_autos_cli_test";
    fs::create_directories(temp_dir);

    // Create top.sv with AUTOINST
    auto top_path = temp_dir / "top.sv";
    {
        std::ofstream ofs(top_path);
        ofs << "module top;\n"
            << "    submod u_sub0 (/*AUTOINST*/);\n"
            << "endmodule\n";
    }

    // Create other.sv with AUTOINST (this should NOT be expanded)
    auto other_path = temp_dir / "other.sv";
    {
        std::ofstream ofs(other_path);
        ofs << "module other;\n"
            << "    submod u_sub1 (/*AUTOINST*/);\n"
            << "endmodule\n";
    }

    // Create submod.sv (the module being instantiated)
    auto submod_path = temp_dir / "submod.sv";
    {
        std::ofstream ofs(submod_path);
        ofs << "module submod(\n"
            << "    input wire clk,\n"
            << "    input wire rst_n\n"
            << ");\n"
            << "endmodule\n";
    }

    // Create a file list that includes other.sv and submod.sv
    auto filelist_path = temp_dir / "files.f";
    {
        std::ofstream ofs(filelist_path);
        ofs << other_path.string() << "\n";
        ofs << submod_path.string() << "\n";
    }

    // Record original content of other.sv
    std::string other_original = readFile(other_path);

    // Run CLI: expand only top.sv, use -f for context
    // Find the binary relative to the test source file
    fs::path binary_path = fs::path(__FILE__).parent_path().parent_path() / "build" / "slang-autos";
    if (!fs::exists(binary_path)) {
        // Try relative to current working directory
        binary_path = fs::current_path() / "slang-autos";
    }
    if (!fs::exists(binary_path)) {
        // Try one directory up (if running from build/tests)
        binary_path = fs::current_path().parent_path() / "slang-autos";
    }
    REQUIRE(fs::exists(binary_path));

    std::string cmd = binary_path.string() + " " + top_path.string() +
                      " -f " + filelist_path.string() + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);

    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int ret = pclose(pipe);

    // Command should succeed
    CHECK(WEXITSTATUS(ret) == 0);

    // Output should mention top.sv was changed
    CHECK(output.find("top.sv") != std::string::npos);
    CHECK(output.find("1 AUTOINST") != std::string::npos);

    // other.sv should NOT have been modified
    std::string other_after = readFile(other_path);
    CHECK(other_original == other_after);

    // top.sv SHOULD have been modified (has port connections now)
    std::string top_after = readFile(top_path);
    CHECK(top_after.find(".clk") != std::string::npos);

    // Cleanup
    fs::remove_all(temp_dir);
}

// =============================================================================
// Idempotency Tests
// =============================================================================

TEST_CASE("Idempotency - expansion produces stable output", "[integration][idempotency]") {
    auto top_sv = getFixturePath("simple/top.sv");
    auto lib_dir = getFixturePath("simple/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool1;
    bool loaded1 = tool1.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded1);

    // First expansion
    auto result1 = tool1.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result1.success);

    // Write result to temp file
    auto temp_dir = fs::temp_directory_path() / "slang_autos_idempotent_test";
    fs::create_directories(temp_dir);
    auto temp_sv = temp_dir / "top.sv";
    {
        std::ofstream ofs(temp_sv);
        ofs << result1.modified_content;
    }

    // Copy lib files
    fs::copy(lib_dir, temp_dir / "lib", fs::copy_options::recursive);

    // Second expansion on the result
    AutosTool tool2;
    bool loaded2 = tool2.loadWithArgs({
        temp_sv.string(),
        "-y", (temp_dir / "lib").string(),
        "+libext+.sv"
    });
    REQUIRE(loaded2);

    auto result2 = tool2.expandFile(temp_sv, /*dry_run=*/true);
    REQUIRE(result2.success);

    // Results should be identical (idempotent)
    CHECK(result1.modified_content == result2.modified_content);

    // Cleanup
    fs::remove_all(temp_dir);
}

TEST_CASE("Idempotency - AUTOWIRE and AUTOPORTS re-expansion is stable", "[integration][idempotency][autowire][autoports]") {
    auto top_sv = getFixturePath("multi_instance/top.sv");
    auto lib_dir = getFixturePath("multi_instance/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool1;
    bool loaded1 = tool1.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded1);

    // First expansion
    auto result1 = tool1.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result1.success);

    // Write result to temp file
    auto temp_dir = fs::temp_directory_path() / "slang_autos_multi_idempotent";
    fs::create_directories(temp_dir);
    auto temp_sv = temp_dir / "top.sv";
    {
        std::ofstream ofs(temp_sv);
        ofs << result1.modified_content;
    }

    // Copy lib files
    fs::copy(lib_dir, temp_dir / "lib", fs::copy_options::recursive);

    // Second expansion
    AutosTool tool2;
    bool loaded2 = tool2.loadWithArgs({
        temp_sv.string(),
        "-y", (temp_dir / "lib").string(),
        "+libext+.sv"
    });
    REQUIRE(loaded2);

    auto result2 = tool2.expandFile(temp_sv, /*dry_run=*/true);
    REQUIRE(result2.success);

    // Results should be identical (idempotent)
    CHECK(result1.modified_content == result2.modified_content);

    // Cleanup
    fs::remove_all(temp_dir);
}

// =============================================================================
// No Duplicate Declarations Tests
// =============================================================================

TEST_CASE("No duplicates - external signals only in AUTOPORTS, not AUTOWIRE", "[integration][no-duplicates]") {
    auto top_sv = getFixturePath("autoports_basic/top.sv");
    auto lib_dir = getFixturePath("autoports_basic/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    CHECK(result.success);

    // In this fixture, all signals are EXTERNAL (single submodule, no instance-to-instance):
    // - clk, rst_n: external inputs (user-declared, should not be generated)
    // - data_in: external input (should be in AUTOPORTS)
    // - data_out, valid: external outputs (should be in AUTOPORTS)
    //
    // AUTOWIRE should generate NOTHING because there are no internal nets
    // (no signal is both driven AND consumed by instances)

    // Find the AUTOWIRE section
    auto autowire_start = result.modified_content.find("/*AUTOWIRE*/");
    REQUIRE(autowire_start != std::string::npos);

    // Check if there's an auto wires section
    auto auto_wires_start = result.modified_content.find("// Beginning of automatic wires");

    if (auto_wires_start != std::string::npos) {
        // If there's an autowire section, it should be empty (no signals declared)
        auto auto_wires_end = result.modified_content.find("// End of automatics", auto_wires_start);
        REQUIRE(auto_wires_end != std::string::npos);

        auto auto_section = result.modified_content.substr(auto_wires_start, auto_wires_end - auto_wires_start);

        // External signals should NOT appear in AUTOWIRE
        CHECK(auto_section.find("data_out") == std::string::npos);
        CHECK(auto_section.find("valid") == std::string::npos);
        CHECK(auto_section.find("data_in") == std::string::npos);
    }

    // External signals SHOULD appear in the port list (AUTOPORTS area)
    // Check that they're in the module header
    auto header_end = result.modified_content.find(");");
    REQUIRE(header_end != std::string::npos);
    auto header = result.modified_content.substr(0, header_end);

    CHECK(header.find("data_out") != std::string::npos);
    CHECK(header.find("valid") != std::string::npos);
    CHECK(header.find("data_in") != std::string::npos);
}

TEST_CASE("Mixed internal/external - correct signal classification", "[integration][no-duplicates]") {
    auto top_sv = getFixturePath("mixed_internal_external/top.sv");
    auto lib_dir = getFixturePath("mixed_internal_external/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    CHECK(result.success);

    // This fixture has:
    // - clk, rst_n: consumed by both producer and consumer inputs, not driven
    //   -> EXTERNAL INPUTS (AUTOPORTS)
    // - data_out, data_valid: driven by producer output, consumed by consumer input
    //   -> INTERNAL LOGIC (AUTOLOGIC)
    // - result, result_valid: driven by consumer output, not consumed
    //   -> EXTERNAL OUTPUTS (AUTOPORTS)

    // Find AUTOLOGIC section
    auto autologic_start = result.modified_content.find("// Beginning of automatic logic");
    REQUIRE(autologic_start != std::string::npos);

    auto autologic_end = result.modified_content.find("// End of automatics", autologic_start);
    REQUIRE(autologic_end != std::string::npos);

    auto autologic_section = result.modified_content.substr(autologic_start, autologic_end - autologic_start);

    // Internal signals SHOULD be in AUTOLOGIC
    CHECK(autologic_section.find("data_out") != std::string::npos);
    CHECK(autologic_section.find("data_valid") != std::string::npos);

    // External signals should NOT be in AUTOLOGIC
    CHECK(autologic_section.find("clk") == std::string::npos);
    CHECK(autologic_section.find("rst_n") == std::string::npos);
    CHECK(autologic_section.find("result") == std::string::npos);
    CHECK(autologic_section.find("result_valid") == std::string::npos);

    // Find the AUTOPORTS section (between /*AUTOPORTS*/ and );)
    auto autoports_start = result.modified_content.find("/*AUTOPORTS*/");
    REQUIRE(autoports_start != std::string::npos);

    auto ports_end = result.modified_content.find(");", autoports_start);
    REQUIRE(ports_end != std::string::npos);

    auto ports_section = result.modified_content.substr(autoports_start, ports_end - autoports_start);

    // External signals SHOULD be in AUTOPORTS
    CHECK(ports_section.find("clk") != std::string::npos);
    CHECK(ports_section.find("rst_n") != std::string::npos);
    CHECK(ports_section.find("result") != std::string::npos);
    CHECK(ports_section.find("result_valid") != std::string::npos);

    // Internal signals should NOT be in AUTOPORTS
    CHECK(ports_section.find("data_out") == std::string::npos);
    CHECK(ports_section.find("data_valid") == std::string::npos);
}

// =============================================================================
// Adversarial / Edge Case Tests
// =============================================================================

TEST_CASE("Adversarial - AUTOINST in string literal is not expanded", "[adversarial]") {
    auto top_sv = getFixturePath("adversarial/autoinst_in_string.sv");
    auto lib_dir = getFixturePath("adversarial/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);
    CHECK(result.hasChanges());
    CHECK(result.autoinst_count == 1);  // Only the real AUTOINST, not the one in string

    // The string literal should remain unchanged
    CHECK(result.modified_content.find("\"Use /*AUTOINST*/ to auto-connect ports\"") != std::string::npos);

    // The real AUTOINST should be expanded (port connections with parens)
    CHECK(result.modified_content.find(".clk") != std::string::npos);
    CHECK(result.modified_content.find("(clk)") != std::string::npos);
    CHECK(result.modified_content.find(".data_in") != std::string::npos);
    CHECK(result.modified_content.find("(data_in)") != std::string::npos);
}

TEST_CASE("Adversarial - wide buses are handled correctly", "[adversarial]") {
    auto top_sv = getFixturePath("adversarial/wide_bus_top.sv");
    auto lib_dir = getFixturePath("adversarial/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);

    // Check that AUTOINST was actually expanded (port connections with parens)
    // The format is ".port_name (signal)" - just check key parts exist
    CHECK(result.modified_content.find(".wide_data_in") != std::string::npos);
    CHECK(result.modified_content.find("(wide_data_in)") != std::string::npos);
    CHECK(result.modified_content.find(".very_wide_data_out") != std::string::npos);
    CHECK(result.modified_content.find("(very_wide_data_out)") != std::string::npos);

    // Verify the expansion happened (original has just /*AUTOINST*/)
    CHECK(result.autoinst_count == 1);
    CHECK(result.hasChanges());
}

TEST_CASE("Adversarial - special instance names", "[adversarial]") {
    auto top_sv = getFixturePath("adversarial/special_names.sv");
    auto lib_dir = getFixturePath("adversarial/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);
    CHECK(result.hasChanges());
    CHECK(result.autoinst_count == 4);  // 4 instances

    // All instances should be expanded with port connections
    CHECK(result.modified_content.find("u_sub_0") != std::string::npos);
    CHECK(result.modified_content.find("u_sub_123") != std::string::npos);
    CHECK(result.modified_content.find("u__double__underscore") != std::string::npos);
    CHECK(result.modified_content.find("u_this_is_a_very_long_instance_name") != std::string::npos);

    // Each should have expanded ports - count .clk( occurrences (4 instances)
    size_t clk_count = 0;
    size_t pos = 0;
    while ((pos = result.modified_content.find(".clk", pos)) != std::string::npos) {
        ++clk_count;
        ++pos;
    }
    CHECK(clk_count == 4);
}

TEST_CASE("Adversarial - input-only and output-only modules", "[adversarial]") {
    auto top_sv = getFixturePath("adversarial/unidirectional.sv");
    auto lib_dir = getFixturePath("adversarial/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);
    CHECK(result.hasChanges());
    CHECK(result.autoinst_count == 2);  // 2 instances

    // Input-only module (sink) should have its inputs connected
    CHECK(result.modified_content.find("u_sink") != std::string::npos);
    CHECK(result.modified_content.find(".data_in") != std::string::npos);

    // Output-only module (source) should have its outputs connected
    CHECK(result.modified_content.find("u_source") != std::string::npos);
    CHECK(result.modified_content.find(".data_out") != std::string::npos);
    CHECK(result.modified_content.find(".valid") != std::string::npos);
}

TEST_CASE("Adversarial - multiple AUTOINST in same module", "[adversarial]") {
    auto top_sv = getFixturePath("adversarial/multiple_autoinst.sv");
    auto lib_dir = getFixturePath("adversarial/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);
    CHECK(result.hasChanges());
    CHECK(result.autoinst_count == 2);  // 2 AUTOINST macros

    // Both instances should be present with expanded ports
    CHECK(result.modified_content.find("u_first") != std::string::npos);
    CHECK(result.modified_content.find("u_second") != std::string::npos);

    // Both should have expanded ports - count .clk occurrences (should be 2)
    size_t clk_count = 0;
    size_t pos = 0;
    while ((pos = result.modified_content.find(".clk", pos)) != std::string::npos) {
        ++clk_count;
        ++pos;
    }
    CHECK(clk_count == 2);

    // Check port connections exist (with parens)
    CHECK(result.modified_content.find("(clk)") != std::string::npos);
    CHECK(result.modified_content.find("(rst_n)") != std::string::npos);
}

// =============================================================================
// Additional Idempotency Tests
// =============================================================================

TEST_CASE("Idempotency - already expanded file has no changes", "[idempotency]") {
    // This file has already been expanded - running again should produce no changes
    auto top_sv = getFixturePath("adversarial/already_expanded.sv");
    auto lib_dir = getFixturePath("adversarial/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    // Read original content
    std::string original = readFile(top_sv);

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);

    // Should have no changes - file is already expanded
    CHECK_FALSE(result.hasChanges());
    CHECK(result.autoinst_count == 0);  // No new expansions
    CHECK(result.modified_content == original);
}

TEST_CASE("Idempotency - AUTOLOGIC expansion is stable", "[idempotency][autologic]") {
    auto top_sv = getFixturePath("autologic/top.sv");
    auto lib_dir = getFixturePath("autologic/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool1;
    bool loaded1 = tool1.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded1);

    // First expansion
    auto result1 = tool1.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result1.success);

    // Write result to temp file
    auto temp_dir = fs::temp_directory_path() / "slang_autos_autologic_idempotent";
    fs::create_directories(temp_dir);
    auto temp_sv = temp_dir / "top.sv";
    {
        std::ofstream ofs(temp_sv);
        ofs << result1.modified_content;
    }

    // Copy lib files
    if (fs::exists(temp_dir / "lib")) {
        fs::remove_all(temp_dir / "lib");
    }
    fs::copy(lib_dir, temp_dir / "lib", fs::copy_options::recursive);

    // Second expansion on the result
    AutosTool tool2;
    bool loaded2 = tool2.loadWithArgs({
        temp_sv.string(),
        "-y", (temp_dir / "lib").string(),
        "+libext+.sv"
    });
    REQUIRE(loaded2);

    auto result2 = tool2.expandFile(temp_sv, /*dry_run=*/true);
    REQUIRE(result2.success);

    // Results should be identical (idempotent)
    CHECK(result1.modified_content == result2.modified_content);

    // Cleanup
    fs::remove_all(temp_dir);
}

TEST_CASE("Idempotency - templates with @ substitution", "[idempotency][templates]") {
    auto top_sv = getFixturePath("templates/top.sv");
    auto lib_dir = getFixturePath("templates/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool1;
    bool loaded1 = tool1.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded1);

    // First expansion
    auto result1 = tool1.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result1.success);

    // Write result to temp file
    auto temp_dir = fs::temp_directory_path() / "slang_autos_template_idempotent";
    fs::create_directories(temp_dir);
    auto temp_sv = temp_dir / "top.sv";
    {
        std::ofstream ofs(temp_sv);
        ofs << result1.modified_content;
    }

    // Copy lib files
    if (fs::exists(temp_dir / "lib")) {
        fs::remove_all(temp_dir / "lib");
    }
    fs::copy(lib_dir, temp_dir / "lib", fs::copy_options::recursive);

    // Second expansion on the result
    AutosTool tool2;
    bool loaded2 = tool2.loadWithArgs({
        temp_sv.string(),
        "-y", (temp_dir / "lib").string(),
        "+libext+.sv"
    });
    REQUIRE(loaded2);

    auto result2 = tool2.expandFile(temp_sv, /*dry_run=*/true);
    REQUIRE(result2.success);

    // Results should be identical (idempotent)
    CHECK(result1.modified_content == result2.modified_content);

    // Cleanup
    fs::remove_all(temp_dir);
}

TEST_CASE("Idempotency - adversarial inputs remain stable", "[idempotency][adversarial]") {
    auto top_sv = getFixturePath("adversarial/special_names.sv");
    auto lib_dir = getFixturePath("adversarial/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool1;
    bool loaded1 = tool1.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded1);

    // First expansion
    auto result1 = tool1.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result1.success);

    // Write result to temp file
    auto temp_dir = fs::temp_directory_path() / "slang_autos_adversarial_idempotent";
    fs::create_directories(temp_dir);
    auto temp_sv = temp_dir / "special_names.sv";
    {
        std::ofstream ofs(temp_sv);
        ofs << result1.modified_content;
    }

    // Copy lib files
    if (fs::exists(temp_dir / "lib")) {
        fs::remove_all(temp_dir / "lib");
    }
    fs::copy(lib_dir, temp_dir / "lib", fs::copy_options::recursive);

    // Second expansion on the result
    AutosTool tool2;
    bool loaded2 = tool2.loadWithArgs({
        temp_sv.string(),
        "-y", (temp_dir / "lib").string(),
        "+libext+.sv"
    });
    REQUIRE(loaded2);

    auto result2 = tool2.expandFile(temp_sv, /*dry_run=*/true);
    REQUIRE(result2.success);

    // Results should be identical (idempotent)
    CHECK(result1.modified_content == result2.modified_content);

    // Cleanup
    fs::remove_all(temp_dir);
}

// =============================================================================
// Width Adaptation Tests
// =============================================================================

TEST_CASE("Width adaptation - slice down when port narrower than signal", "[width]") {
    auto top_sv = getFixturePath("width_adaptation/top_slice.sv");
    auto lib_dir = getFixturePath("width_adaptation/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);

    // wide (16-bit) should get full width connection
    CHECK(result.modified_content.find(".data_in  (data_in)") != std::string::npos);
    CHECK(result.modified_content.find(".data_out (data_out)") != std::string::npos);

    // narrow (8-bit) should get sliced connections
    CHECK(result.modified_content.find(".data_in  (data_in[7:0])") != std::string::npos);
    CHECK(result.modified_content.find(".data_out (data_out[7:0])") != std::string::npos);
}

TEST_CASE("Width adaptation - single-bit uses [0] not [0:0]", "[width]") {
    auto top_sv = getFixturePath("width_adaptation/top_single_bit.sv");
    auto lib_dir = getFixturePath("width_adaptation/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);

    // one_bit (1-bit) should get [0] slice, not [0:0]
    CHECK(result.modified_content.find(".data_in  (data_in[0])") != std::string::npos);
    CHECK(result.modified_content.find(".data_out (data_out[0])") != std::string::npos);

    // Should NOT have [0:0] format
    CHECK(result.modified_content.find("[0:0]") == std::string::npos);
}

TEST_CASE("Width adaptation - existing multi_instance fixture slices correctly", "[width]") {
    // This test uses the existing multi_instance fixture which has:
    // producer with 16-bit data output
    // consumer with 8-bit data input
    auto top_sv = getFixturePath("multi_instance/top.sv");
    auto lib_dir = getFixturePath("multi_instance/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool;
    bool loaded = tool.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded);

    auto result = tool.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result.success);

    // Producer (16-bit output) connects to full signal
    CHECK(result.modified_content.find("u_producer") != std::string::npos);
    // Look for producer's .data connection - should be unsliced
    // Producer's data output is full width
    size_t producer_pos = result.modified_content.find("u_producer");
    size_t consumer_pos = result.modified_content.find("u_consumer");
    REQUIRE(producer_pos != std::string::npos);
    REQUIRE(consumer_pos != std::string::npos);

    // Extract producer section
    std::string producer_section = result.modified_content.substr(
        producer_pos, consumer_pos - producer_pos);
    // Producer should have .data (data) - full width
    CHECK(producer_section.find(".data  (data)") != std::string::npos);

    // Extract consumer section
    std::string consumer_section = result.modified_content.substr(consumer_pos);
    // Consumer should have .data (data[7:0]) - sliced to 8-bit
    CHECK(consumer_section.find(".data         (data[7:0])") != std::string::npos);
}

TEST_CASE("Width adaptation - idempotency with slices", "[width][idempotency]") {
    auto top_sv = getFixturePath("width_adaptation/top_slice.sv");
    auto lib_dir = getFixturePath("width_adaptation/lib");

    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    AutosTool tool1;
    bool loaded1 = tool1.loadWithArgs({
        top_sv.string(),
        "-y", lib_dir.string(),
        "+libext+.sv"
    });
    REQUIRE(loaded1);

    // First expansion
    auto result1 = tool1.expandFile(top_sv, /*dry_run=*/true);
    REQUIRE(result1.success);

    // Write result to temp file
    auto temp_dir = fs::temp_directory_path() / "slang_autos_width_idempotent";
    fs::create_directories(temp_dir);
    auto temp_sv = temp_dir / "top_slice.sv";
    {
        std::ofstream ofs(temp_sv);
        ofs << result1.modified_content;
    }

    // Copy lib files
    if (fs::exists(temp_dir / "lib")) {
        fs::remove_all(temp_dir / "lib");
    }
    fs::copy(lib_dir, temp_dir / "lib", fs::copy_options::recursive);

    // Second expansion on the result
    AutosTool tool2;
    bool loaded2 = tool2.loadWithArgs({
        temp_sv.string(),
        "-y", (temp_dir / "lib").string(),
        "+libext+.sv"
    });
    REQUIRE(loaded2);

    auto result2 = tool2.expandFile(temp_sv, /*dry_run=*/true);
    REQUIRE(result2.success);

    // Results should be identical (idempotent)
    CHECK(result1.modified_content == result2.modified_content);

    // Cleanup
    fs::remove_all(temp_dir);
}
