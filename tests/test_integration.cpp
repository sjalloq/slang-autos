// Integration tests for slang-autos
// These tests exercise the full slang driver flow with real SystemVerilog files

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
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
// AUTOWIRE Tests
// =============================================================================

TEST_CASE("AUTOWIRE - basic wire generation", "[integration][autowire]") {
    auto top_sv = getFixturePath("autowire_basic/top.sv");
    auto lib_dir = getFixturePath("autowire_basic/lib");

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
    // AUTOWIRE should generate declarations for instance outputs
    CHECK(result.modified_content.find("// Beginning of automatic wires") != std::string::npos);
    CHECK(result.modified_content.find("logic [7:0] data_out") != std::string::npos);
    CHECK(result.modified_content.find("logic valid") != std::string::npos);
    CHECK(result.modified_content.find("// End of automatics") != std::string::npos);
}

TEST_CASE("AUTOWIRE - skips user-declared signals", "[integration][autowire]") {
    auto top_sv = getFixturePath("autowire_skip_declared/top.sv");
    auto lib_dir = getFixturePath("autowire_skip_declared/lib");

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
    // valid is not declared, should appear in AUTOWIRE section
    CHECK(result.modified_content.find("logic valid") != std::string::npos);

    // data_out is user-declared, should NOT appear in AUTOWIRE section
    // Check the section between markers specifically
    auto auto_start = result.modified_content.find("// Beginning of automatic wires");
    auto auto_end = result.modified_content.find("// End of automatics");
    REQUIRE(auto_start != std::string::npos);
    REQUIRE(auto_end != std::string::npos);
    auto auto_section = result.modified_content.substr(auto_start, auto_end - auto_start);

    // data_out should NOT be in the autowire section
    CHECK(auto_section.find("data_out") == std::string::npos);
    // valid SHOULD be in the autowire section
    CHECK(auto_section.find("valid") != std::string::npos);
}

TEST_CASE("AUTOWIRE - width conflict resolution takes max", "[integration][autowire]") {
    auto top_sv = getFixturePath("autowire_width_conflict/top.sv");
    auto lib_dir = getFixturePath("autowire_width_conflict/lib");

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
    // shared_bus should be declared with max width [15:0], not [7:0]
    CHECK(result.modified_content.find("logic [15:0] shared_bus") != std::string::npos);
    CHECK(result.modified_content.find("logic [7:0] shared_bus") == std::string::npos);
}

TEST_CASE("AUTOWIRE - template rename uses new signal name", "[integration][autowire]") {
    auto top_sv = getFixturePath("autowire_template_rename/top.sv");
    auto lib_dir = getFixturePath("autowire_template_rename/lib");

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
    // Template renames data_out to renamed_signal
    CHECK(result.modified_content.find("logic [7:0] renamed_signal") != std::string::npos);
    // data_out should NOT appear in AUTOWIRE (it's renamed)
    // Note: data_out appears in the template rule, so check the auto wires section specifically
    auto auto_start = result.modified_content.find("// Beginning of automatic wires");
    auto auto_end = result.modified_content.find("// End of automatics");
    if (auto_start != std::string::npos && auto_end != std::string::npos) {
        auto auto_section = result.modified_content.substr(auto_start, auto_end - auto_start);
        CHECK(auto_section.find("data_out") == std::string::npos);
    }
}

TEST_CASE("AUTOWIRE - constants and disconnects don't generate wires", "[integration][autowire]") {
    auto top_sv = getFixturePath("autowire_constants/top.sv");
    auto lib_dir = getFixturePath("autowire_constants/lib");

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
    // Both outputs are mapped to constants/disconnects, so AUTOWIRE should generate nothing
    // (or just the markers with nothing between them)
    auto auto_start = result.modified_content.find("// Beginning of automatic wires");
    if (auto_start != std::string::npos) {
        auto auto_end = result.modified_content.find("// End of automatics");
        if (auto_end != std::string::npos) {
            auto auto_section = result.modified_content.substr(auto_start, auto_end - auto_start);
            // Should not contain any logic declarations
            CHECK(auto_section.find("logic") == std::string::npos);
        }
    }
}

TEST_CASE("AUTOWIRE - inout ports generate wires", "[integration][autowire]") {
    auto top_sv = getFixturePath("autowire_inout/top.sv");
    auto lib_dir = getFixturePath("autowire_inout/lib");

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
    // inout port should generate a wire declaration
    CHECK(result.modified_content.find("logic [7:0] bus") != std::string::npos);
}

TEST_CASE("AUTOWIRE - re-expansion replaces old content", "[integration][autowire]") {
    auto top_sv = getFixturePath("autowire_reexpand/top.sv");
    auto lib_dir = getFixturePath("autowire_reexpand/lib");

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
    // old_signal should be removed (it was in the old expansion)
    CHECK(result.modified_content.find("old_signal") == std::string::npos);
    // New content should be generated
    CHECK(result.modified_content.find("data_out") != std::string::npos);
    CHECK(result.modified_content.find("valid") != std::string::npos);
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
    // Note: test runs from build/tests/, binary is in build/
    std::string cmd = "../slang-autos " + top_path.string() +
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
