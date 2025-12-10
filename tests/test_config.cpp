#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "slang-autos/Config.h"
#include "slang-autos/Parser.h"
#include "slang-autos/Tool.h"

using namespace slang_autos;
namespace fs = std::filesystem;

// Helper to create a temp file with content
class TempFile {
public:
    TempFile(const std::string& content, const std::string& name = ".slang-autos.toml") {
        path_ = fs::temp_directory_path() / ("test_config_" + std::to_string(counter_++));
        fs::create_directories(path_);
        file_path_ = path_ / name;
        std::ofstream ofs(file_path_);
        ofs << content;
    }

    ~TempFile() {
        fs::remove_all(path_);
    }

    const fs::path& dir() const { return path_; }
    const fs::path& file() const { return file_path_; }

private:
    fs::path path_;
    fs::path file_path_;
    static inline int counter_ = 0;
};

// ============================================================================
// FileConfig Loading Tests
// ============================================================================

TEST_CASE("ConfigLoader::loadFile - parses library section", "[config]") {
    TempFile temp(R"(
[library]
libdirs = ["./lib", "./rtl/common"]
libext = [".v", ".sv"]
incdirs = ["./include"]
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->libdirs.has_value());
    CHECK(config->libdirs->size() == 2);
    CHECK((*config->libdirs)[0] == "./lib");
    CHECK((*config->libdirs)[1] == "./rtl/common");

    REQUIRE(config->libext.has_value());
    CHECK(config->libext->size() == 2);

    REQUIRE(config->incdirs.has_value());
    CHECK(config->incdirs->size() == 1);
}

TEST_CASE("ConfigLoader::loadFile - parses formatting section", "[config]") {
    TempFile temp(R"(
[formatting]
indent = 2
alignment = false
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->indent.has_value());
    CHECK(*config->indent == 2);
    REQUIRE(config->alignment.has_value());
    CHECK(*config->alignment == false);
}

TEST_CASE("ConfigLoader::loadFile - parses tab indent", "[config]") {
    TempFile temp(R"(
[formatting]
indent = "tab"
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->indent.has_value());
    CHECK(*config->indent == -1);  // -1 = tab
}

TEST_CASE("ConfigLoader::loadFile - parses behavior section", "[config]") {
    TempFile temp(R"(
[behavior]
strictness = "strict"
verbosity = 2
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->strictness.has_value());
    CHECK(*config->strictness == StrictnessMode::Strict);
    REQUIRE(config->verbosity.has_value());
    CHECK(*config->verbosity == 2);
}

TEST_CASE("ConfigLoader::loadFile - parses single_unit", "[config]") {
    TempFile temp(R"(
[behavior]
single_unit = false
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->single_unit.has_value());
    CHECK(*config->single_unit == false);
}

TEST_CASE("ConfigLoader::loadFile - handles missing sections", "[config]") {
    TempFile temp(R"(
[formatting]
indent = 4
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    CHECK_FALSE(config->libdirs.has_value());
    CHECK_FALSE(config->strictness.has_value());
    CHECK(config->indent.has_value());
}

TEST_CASE("ConfigLoader::loadFile - returns nullopt on invalid TOML", "[config]") {
    TempFile temp("this is not valid toml [[[");

    DiagnosticCollector diag;
    auto config = ConfigLoader::loadFile(temp.file(), &diag);

    CHECK_FALSE(config.has_value());
    CHECK(diag.hasErrors());
}

// ============================================================================
// Config Discovery Tests
// ============================================================================

TEST_CASE("ConfigLoader::findConfigFile - finds file in start dir", "[config]") {
    TempFile temp("[formatting]\nindent = 4\n");

    auto found = ConfigLoader::findConfigFile(temp.dir());

    REQUIRE(found.has_value());
    CHECK(*found == temp.file());
}

TEST_CASE("ConfigLoader::findConfigFile - returns nullopt when not found", "[config]") {
    fs::path empty_dir = fs::temp_directory_path() / "empty_test_dir";
    fs::create_directories(empty_dir);

    auto found = ConfigLoader::findConfigFile(empty_dir);

    CHECK_FALSE(found.has_value());

    fs::remove_all(empty_dir);
}

// ============================================================================
// Config Merging Tests
// ============================================================================

TEST_CASE("ConfigLoader::merge - uses defaults when no config", "[config]") {
    InlineConfig inline_cfg;
    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(std::nullopt, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.indent == "  ");  // 2 spaces default
    CHECK(merged.alignment == true);
    CHECK(merged.strictness == StrictnessMode::Lenient);
    CHECK(merged.verbosity == 1);
    CHECK(merged.single_unit == true);  // Default: enabled
}

TEST_CASE("ConfigLoader::merge - single_unit from file config", "[config]") {
    FileConfig file_cfg;
    file_cfg.single_unit = false;

    InlineConfig inline_cfg;
    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.single_unit == false);
}

TEST_CASE("ConfigLoader::merge - CLI overrides single_unit from file", "[config]") {
    FileConfig file_cfg;
    file_cfg.single_unit = false;

    InlineConfig inline_cfg;
    AutosToolOptions cli_opts;
    cli_opts.single_unit = true;
    CliFlags cli_flags;
    cli_flags.has_single_unit = true;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.single_unit == true);  // CLI wins
}

TEST_CASE("ConfigLoader::merge - file config overrides defaults", "[config]") {
    FileConfig file_cfg;
    file_cfg.indent = 2;
    file_cfg.alignment = false;
    file_cfg.strictness = StrictnessMode::Strict;

    InlineConfig inline_cfg;
    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.indent == "  ");  // 2 spaces
    CHECK(merged.alignment == false);
    CHECK(merged.strictness == StrictnessMode::Strict);
}

TEST_CASE("ConfigLoader::merge - CLI overrides file config", "[config]") {
    FileConfig file_cfg;
    file_cfg.indent = 2;
    file_cfg.strictness = StrictnessMode::Lenient;

    InlineConfig inline_cfg;

    AutosToolOptions cli_opts;
    cli_opts.indent = "        ";  // 8 spaces
    cli_opts.strictness = StrictnessMode::Strict;

    CliFlags cli_flags;
    cli_flags.has_indent = true;
    cli_flags.has_strictness = true;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.indent == "        ");  // CLI wins
    CHECK(merged.strictness == StrictnessMode::Strict);  // CLI wins
}

TEST_CASE("ConfigLoader::merge - library paths are additive", "[config]") {
    FileConfig file_cfg;
    file_cfg.libdirs = std::vector<std::string>{"./lib1"};
    file_cfg.libext = std::vector<std::string>{".v"};

    InlineConfig inline_cfg;
    inline_cfg.libdirs = {"./lib2"};
    inline_cfg.libext = {".sv"};

    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.libdirs.size() == 2);
    CHECK(merged.libdirs[0] == "./lib1");
    CHECK(merged.libdirs[1] == "./lib2");
    CHECK(merged.libext.size() == 2);
}

TEST_CASE("ConfigLoader::merge - tab indent works", "[config]") {
    FileConfig file_cfg;
    file_cfg.indent = -1;  // tab

    InlineConfig inline_cfg;
    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.indent == "\t");
}

// ============================================================================
// MergedConfig Methods
// ============================================================================

TEST_CASE("MergedConfig::toToolOptions - converts correctly", "[config]") {
    MergedConfig merged;
    merged.indent = "  ";
    merged.alignment = false;
    merged.strictness = StrictnessMode::Strict;
    merged.verbosity = 2;

    auto opts = merged.toToolOptions();

    CHECK(opts.indent == "  ");
    CHECK(opts.alignment == false);
    CHECK(opts.strictness == StrictnessMode::Strict);
    CHECK(opts.verbosity == 2);
}

TEST_CASE("MergedConfig::getSlangArgs - generates correct arguments", "[config]") {
    MergedConfig merged;
    merged.libdirs = {"./lib1", "./lib2"};
    merged.libext = {".v", ".sv"};
    merged.incdirs = {"./include"};

    auto args = merged.getSlangArgs();

    // Expected: --single-unit -y ./lib1 -y ./lib2 +libext+.v +libext+.sv +incdir+./include
    // (single_unit defaults to true)
    REQUIRE(args.size() == 8);
    CHECK(args[0] == "--single-unit");
    CHECK(args[1] == "-y");
    CHECK(args[2] == "./lib1");
    CHECK(args[3] == "-y");
    CHECK(args[4] == "./lib2");
    CHECK(args[5] == "+libext+.v");
    CHECK(args[6] == "+libext+.sv");
    CHECK(args[7] == "+incdir+./include");
}

TEST_CASE("MergedConfig::getSlangArgs - single_unit disabled", "[config]") {
    MergedConfig merged;
    merged.single_unit = false;
    merged.libdirs = {"./lib"};

    auto args = merged.getSlangArgs();

    // --single-unit should NOT be present
    REQUIRE(args.size() == 2);
    CHECK(args[0] == "-y");
    CHECK(args[1] == "./lib");
}

// ============================================================================
// Extended Inline Config Parsing
// ============================================================================

TEST_CASE("parseInlineConfig - parses incdir", "[config]") {
    std::string content = R"(
module test;
endmodule
// slang-autos-incdir: ./include ./inc2
)";

    auto config = parseInlineConfig(content);

    REQUIRE(config.incdirs.size() == 2);
    CHECK(config.incdirs[0] == "./include");
    CHECK(config.incdirs[1] == "./inc2");
}

TEST_CASE("parseInlineConfig - parses indent", "[config]") {
    std::string content = "// slang-autos-indent: 2\n";

    auto config = parseInlineConfig(content);

    REQUIRE(config.indent.has_value());
    CHECK(*config.indent == 2);
}

TEST_CASE("parseInlineConfig - parses tab indent", "[config]") {
    std::string content = "// slang-autos-indent: tab\n";

    auto config = parseInlineConfig(content);

    REQUIRE(config.indent.has_value());
    CHECK(*config.indent == -1);
}

TEST_CASE("parseInlineConfig - parses alignment true", "[config]") {
    std::string content = "// slang-autos-alignment: true\n";

    auto config = parseInlineConfig(content);

    REQUIRE(config.alignment.has_value());
    CHECK(*config.alignment == true);
}

TEST_CASE("parseInlineConfig - parses alignment false", "[config]") {
    std::string content = "// slang-autos-alignment: no\n";

    auto config = parseInlineConfig(content);

    REQUIRE(config.alignment.has_value());
    CHECK(*config.alignment == false);
}

TEST_CASE("parseInlineConfig - parses strictness", "[config]") {
    std::string content = "// slang-autos-strictness: strict\n";

    auto config = parseInlineConfig(content);

    REQUIRE(config.strictness.has_value());
    CHECK(*config.strictness == StrictnessMode::Strict);
}

// ============================================================================
// Environment Variable Expansion Tests
// ============================================================================

TEST_CASE("expandEnvironmentVariables - expands $VAR form", "[config]") {
    // Set a test environment variable
    setenv("SLANG_AUTOS_TEST_VAR", "/test/path", 1);

    CHECK(expandEnvironmentVariables("$SLANG_AUTOS_TEST_VAR") == "/test/path");
    CHECK(expandEnvironmentVariables("$SLANG_AUTOS_TEST_VAR/lib") == "/test/path/lib");
    CHECK(expandEnvironmentVariables("prefix/$SLANG_AUTOS_TEST_VAR/suffix") == "prefix//test/path/suffix");

    unsetenv("SLANG_AUTOS_TEST_VAR");
}

TEST_CASE("expandEnvironmentVariables - expands ${VAR} form", "[config]") {
    setenv("SLANG_AUTOS_TEST_VAR", "/braced/path", 1);

    CHECK(expandEnvironmentVariables("${SLANG_AUTOS_TEST_VAR}") == "/braced/path");
    CHECK(expandEnvironmentVariables("${SLANG_AUTOS_TEST_VAR}/lib") == "/braced/path/lib");
    CHECK(expandEnvironmentVariables("prefix/${SLANG_AUTOS_TEST_VAR}/suffix") == "prefix//braced/path/suffix");

    unsetenv("SLANG_AUTOS_TEST_VAR");
}

TEST_CASE("expandEnvironmentVariables - expands $(VAR) form", "[config]") {
    setenv("SLANG_AUTOS_TEST_VAR", "/paren/path", 1);

    CHECK(expandEnvironmentVariables("$(SLANG_AUTOS_TEST_VAR)") == "/paren/path");
    CHECK(expandEnvironmentVariables("$(SLANG_AUTOS_TEST_VAR)/lib") == "/paren/path/lib");

    unsetenv("SLANG_AUTOS_TEST_VAR");
}

TEST_CASE("expandEnvironmentVariables - unset variables report error", "[config]") {
    // Make sure the variable doesn't exist
    unsetenv("SLANG_AUTOS_NONEXISTENT_VAR");

    DiagnosticCollector diag;

    // Without diagnostics, still expands to empty (for backward compat)
    CHECK(expandEnvironmentVariables("$SLANG_AUTOS_NONEXISTENT_VAR") == "");

    // With diagnostics, errors are reported
    CHECK(expandEnvironmentVariables("$SLANG_AUTOS_NONEXISTENT_VAR", &diag) == "");
    REQUIRE(diag.errorCount() == 1);
    CHECK(diag.diagnostics()[0].message.find("SLANG_AUTOS_NONEXISTENT_VAR") != std::string::npos);
    CHECK(diag.diagnostics()[0].message.find("not set") != std::string::npos);

    diag.clear();
    CHECK(expandEnvironmentVariables("${SLANG_AUTOS_NONEXISTENT_VAR}", &diag) == "");
    REQUIRE(diag.errorCount() == 1);

    diag.clear();
    CHECK(expandEnvironmentVariables("prefix/$SLANG_AUTOS_NONEXISTENT_VAR/suffix", &diag) == "prefix//suffix");
    REQUIRE(diag.errorCount() == 1);
}

TEST_CASE("expandEnvironmentVariables - preserves non-variable text", "[config]") {
    CHECK(expandEnvironmentVariables("no variables here") == "no variables here");
    CHECK(expandEnvironmentVariables("./relative/path") == "./relative/path");
    CHECK(expandEnvironmentVariables("/absolute/path") == "/absolute/path");
}

TEST_CASE("expandEnvironmentVariables - handles edge cases", "[config]") {
    CHECK(expandEnvironmentVariables("") == "");
    CHECK(expandEnvironmentVariables("$") == "$");
    CHECK(expandEnvironmentVariables("$$") == "$$");
    CHECK(expandEnvironmentVariables("${") == "${");
    CHECK(expandEnvironmentVariables("${unclosed") == "${unclosed");
    CHECK(expandEnvironmentVariables("$(unclosed") == "$(unclosed");
}

TEST_CASE("expandEnvironmentVariables - multiple variables", "[config]") {
    setenv("SLANG_AUTOS_VAR1", "first", 1);
    setenv("SLANG_AUTOS_VAR2", "second", 1);

    CHECK(expandEnvironmentVariables("$SLANG_AUTOS_VAR1/$SLANG_AUTOS_VAR2") == "first/second");
    CHECK(expandEnvironmentVariables("${SLANG_AUTOS_VAR1}${SLANG_AUTOS_VAR2}") == "firstsecond");

    unsetenv("SLANG_AUTOS_VAR1");
    unsetenv("SLANG_AUTOS_VAR2");
}

TEST_CASE("parseInlineConfig - expands environment variables in libdir", "[config]") {
    setenv("SLANG_AUTOS_LIB", "/custom/lib", 1);

    std::string content = "// slang-autos-libdir: $SLANG_AUTOS_LIB\n";
    auto config = parseInlineConfig(content);

    REQUIRE(config.libdirs.size() == 1);
    CHECK(config.libdirs[0] == "/custom/lib");

    unsetenv("SLANG_AUTOS_LIB");
}

TEST_CASE("parseInlineConfig - expands ${VAR} in libdir", "[config]") {
    setenv("SLANG_AUTOS_ROOT", "/project", 1);

    std::string content = "// slang-autos-libdir: ${SLANG_AUTOS_ROOT}/rtl ${SLANG_AUTOS_ROOT}/lib\n";
    auto config = parseInlineConfig(content);

    REQUIRE(config.libdirs.size() == 2);
    CHECK(config.libdirs[0] == "/project/rtl");
    CHECK(config.libdirs[1] == "/project/lib");

    unsetenv("SLANG_AUTOS_ROOT");
}

TEST_CASE("parseInlineConfig - expands environment variables in incdir", "[config]") {
    setenv("SLANG_AUTOS_INC", "/include/path", 1);

    std::string content = "// slang-autos-incdir: $SLANG_AUTOS_INC\n";
    auto config = parseInlineConfig(content);

    REQUIRE(config.incdirs.size() == 1);
    CHECK(config.incdirs[0] == "/include/path");

    unsetenv("SLANG_AUTOS_INC");
}
