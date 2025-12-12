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
// Multi-Instance Comprehensive Test (AUTOWIRE + AUTOPORTS)
// =============================================================================

TEST_CASE("Multi-instance - signal flow and classification", "[integration][autowire][autoports]") {
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
    // - data, valid: driven by producer, consumed by consumer -> INTERNAL WIRES
    // - result, result_valid: driven by consumer, not consumed -> EXTERNAL OUTPUTS
    // - Width conflict: producer outputs 16-bit data, consumer inputs 8-bit -> max (16-bit)
    // - User-declared signal: user_declared_wire should be skipped

    // === AUTOWIRE section checks ===
    auto autowire_start = result.modified_content.find("// Beginning of automatic wires");
    REQUIRE(autowire_start != std::string::npos);

    auto autowire_end = result.modified_content.find("// End of automatics", autowire_start);
    REQUIRE(autowire_end != std::string::npos);

    auto autowire_section = result.modified_content.substr(autowire_start, autowire_end - autowire_start);

    // Internal wires SHOULD be in AUTOWIRE
    CHECK(autowire_section.find("data") != std::string::npos);
    CHECK(autowire_section.find("valid") != std::string::npos);

    // Width conflict: data should be 16-bit (max of 16 and 8)
    CHECK(autowire_section.find("[15:0]") != std::string::npos);

    // External signals should NOT be in AUTOWIRE
    CHECK(autowire_section.find("clk") == std::string::npos);
    CHECK(autowire_section.find("rst_n") == std::string::npos);
    CHECK(autowire_section.find("result") == std::string::npos);
    CHECK(autowire_section.find("result_valid") == std::string::npos);

    // User-declared signal should NOT be in AUTOWIRE
    CHECK(autowire_section.find("user_declared_wire") == std::string::npos);

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
    //   -> INTERNAL WIRES (AUTOWIRE)
    // - result, result_valid: driven by consumer output, not consumed
    //   -> EXTERNAL OUTPUTS (AUTOPORTS)

    // Find AUTOWIRE section
    auto auto_wires_start = result.modified_content.find("// Beginning of automatic wires");
    REQUIRE(auto_wires_start != std::string::npos);

    auto auto_wires_end = result.modified_content.find("// End of automatics", auto_wires_start);
    REQUIRE(auto_wires_end != std::string::npos);

    auto autowire_section = result.modified_content.substr(auto_wires_start, auto_wires_end - auto_wires_start);

    // Internal signals SHOULD be in AUTOWIRE
    CHECK(autowire_section.find("data_out") != std::string::npos);
    CHECK(autowire_section.find("data_valid") != std::string::npos);

    // External signals should NOT be in AUTOWIRE
    CHECK(autowire_section.find("clk") == std::string::npos);
    CHECK(autowire_section.find("rst_n") == std::string::npos);
    CHECK(autowire_section.find("result") == std::string::npos);
    CHECK(autowire_section.find("result_valid") == std::string::npos);

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
