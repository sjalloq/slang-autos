// Tests for DotStarExpander - .* wildcard port expansion

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "slang-autos/DotStarExpander.h"
#include "slang-autos/Writer.h"

#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

namespace fs = std::filesystem;
using namespace slang_autos;

// Helper to get path to test fixtures
static fs::path getFixturePath(const std::string& relative) {
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
    return candidates[0];
}

static std::string readFile(const fs::path& path) {
    std::ifstream ifs(path);
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

/// Helper: set up a slang compilation from a top file + library dir
static std::unique_ptr<slang::ast::Compilation> createCompilation(
    slang::driver::Driver& driver,
    const fs::path& top_sv,
    const fs::path& lib_dir) {

    driver.addStandardArgs();

    std::vector<const char*> args;
    args.push_back("slang-expand");
    std::string top_str = top_sv.string();
    std::string lib_str = lib_dir.string();
    std::string libext = "+libext+.sv";
    std::string libdir = "-y";
    args.push_back(top_str.c_str());
    args.push_back(libdir.c_str());
    args.push_back(lib_str.c_str());
    args.push_back(libext.c_str());

    driver.parseCommandLine(static_cast<int>(args.size()), args.data());
    driver.processOptions();
    driver.options.compilationFlags[slang::ast::CompilationFlags::IgnoreUnknownModules] = true;
    driver.options.topModules = {top_sv.stem().string()};
    driver.parseAllSources();

    return driver.createCompilation();
}

// =============================================================================
// Basic .* expansion
// =============================================================================

TEST_CASE("DotStar - pure .* expansion", "[dotstar]") {
    auto top_sv = getFixturePath("dotstar_simple/top.sv");
    auto lib_dir = getFixturePath("dotstar_simple/lib");
    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    slang::driver::Driver driver;
    auto compilation = createCompilation(driver, top_sv, lib_dir);
    REQUIRE(compilation);

    std::string source = readFile(top_sv);
    auto tree = slang::syntax::SyntaxTree::fromText(source);
    REQUIRE(tree);

    DotStarExpander expander(*compilation);
    expander.analyze(tree, source);

    CHECK(expander.expandedCount() == 1);

    auto& repls = expander.getReplacements();
    REQUIRE_FALSE(repls.empty());

    SourceWriter writer(false);
    std::string result = writer.applyReplacements(source, repls);

    // All ports should be expanded
    CHECK(result.find(".clk") != std::string::npos);
    CHECK(result.find(".rst_n") != std::string::npos);
    CHECK(result.find(".data_in") != std::string::npos);
    CHECK(result.find(".data_out") != std::string::npos);
    CHECK(result.find(".valid") != std::string::npos);

    // .* should be gone
    CHECK(result.find(".*") == std::string::npos);
}

TEST_CASE("DotStar - mixed explicit + .* expansion", "[dotstar]") {
    auto top_sv = getFixturePath("dotstar_mixed/top.sv");
    auto lib_dir = getFixturePath("dotstar_mixed/lib");
    REQUIRE(fs::exists(top_sv));
    REQUIRE(fs::exists(lib_dir));

    slang::driver::Driver driver;
    auto compilation = createCompilation(driver, top_sv, lib_dir);
    REQUIRE(compilation);

    std::string source = readFile(top_sv);
    auto tree = slang::syntax::SyntaxTree::fromText(source);
    REQUIRE(tree);

    DotStarExpander expander(*compilation);
    expander.analyze(tree, source);

    CHECK(expander.expandedCount() == 1);

    auto& repls = expander.getReplacements();
    REQUIRE_FALSE(repls.empty());

    SourceWriter writer(false);
    std::string result = writer.applyReplacements(source, repls);

    // Explicit ports should remain
    CHECK(result.find(".clk") != std::string::npos);
    CHECK(result.find(".rst_n") != std::string::npos);

    // Wildcard ports should be expanded
    CHECK(result.find(".data_in") != std::string::npos);
    CHECK(result.find(".data_out") != std::string::npos);
    CHECK(result.find(".valid") != std::string::npos);

    // .* should be gone
    CHECK(result.find(".*") == std::string::npos);

    // Explicit connections should still have their original form
    CHECK(result.find(".clk     (clk)") != std::string::npos);
}

TEST_CASE("DotStar - no .* in file", "[dotstar]") {
    // Use an existing fixture that has no .*
    auto top_sv = getFixturePath("dotstar_mixed/lib/submod.sv");
    REQUIRE(fs::exists(top_sv));

    slang::driver::Driver driver;
    driver.addStandardArgs();

    std::string top_str = top_sv.string();
    std::vector<const char*> args = {"slang-expand", top_str.c_str()};
    driver.parseCommandLine(2, args.data());
    driver.processOptions();
    driver.options.compilationFlags[slang::ast::CompilationFlags::IgnoreUnknownModules] = true;
    driver.options.topModules = {"submod"};
    driver.parseAllSources();
    auto compilation = driver.createCompilation();
    REQUIRE(compilation);

    std::string source = readFile(top_sv);
    auto tree = slang::syntax::SyntaxTree::fromText(source);
    REQUIRE(tree);

    DotStarExpander expander(*compilation);
    expander.analyze(tree, source);

    CHECK(expander.expandedCount() == 0);
    CHECK(expander.getReplacements().empty());
}
