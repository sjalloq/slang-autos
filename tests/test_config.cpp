#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "slang-autos/Config.h"
#include "slang-autos/Parser.h"
#include "slang-autos/SignalAggregator.h"
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
libdir = ["./lib", "./rtl/common"]
libext = [".v", ".sv"]
incdir = ["./include"]
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

TEST_CASE("parseInlineConfig - parses resolved-ranges", "[config]") {
    // Key has a hyphen - tests that the regex captures hyphenated keys correctly
    std::string content = "// slang-autos-resolved-ranges: true\n";

    auto config = parseInlineConfig(content);

    REQUIRE(config.resolved_ranges.has_value());
    CHECK(*config.resolved_ranges == true);

    // Also test false
    std::string content2 = "// slang-autos-resolved-ranges: false\n";
    auto config2 = parseInlineConfig(content2);

    REQUIRE(config2.resolved_ranges.has_value());
    CHECK(*config2.resolved_ranges == false);
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

// ============================================================================
// Config File Discovery - alternate filename
// ============================================================================

TEST_CASE("ConfigLoader::findConfigFile - finds .slang-autos without .toml extension", "[config]") {
    TempFile temp("[formatting]\nindent = 4\n", ".slang-autos");

    auto found = ConfigLoader::findConfigFile(temp.dir());

    REQUIRE(found.has_value());
    CHECK(found->filename() == ".slang-autos");
}

TEST_CASE("ConfigLoader::findConfigFile - .slang-autos.toml takes priority over .slang-autos", "[config]") {
    // Create a temp directory with both files
    fs::path dir = fs::temp_directory_path() / "test_config_priority";
    fs::create_directories(dir);

    std::ofstream(dir / ".slang-autos.toml") << "[formatting]\nindent = 2\n";
    std::ofstream(dir / ".slang-autos") << "[formatting]\nindent = 4\n";

    auto found = ConfigLoader::findConfigFile(dir);

    REQUIRE(found.has_value());
    CHECK(found->filename() == ".slang-autos.toml");

    fs::remove_all(dir);
}

// ============================================================================
// TOML Environment Variable Expansion
// ============================================================================

TEST_CASE("ConfigLoader::loadFile - expands env vars in libdirs", "[config]") {
    setenv("SLANG_AUTOS_TEST_ROOT", "/project/root", 1);

    TempFile temp(R"(
[library]
libdir = ["$SLANG_AUTOS_TEST_ROOT/rtl", "${SLANG_AUTOS_TEST_ROOT}/lib"]
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->libdirs.has_value());
    CHECK(config->libdirs->size() == 2);
    CHECK((*config->libdirs)[0] == "/project/root/rtl");
    CHECK((*config->libdirs)[1] == "/project/root/lib");

    unsetenv("SLANG_AUTOS_TEST_ROOT");
}

TEST_CASE("ConfigLoader::loadFile - expands env vars in incdirs", "[config]") {
    setenv("SLANG_AUTOS_TEST_INC", "/inc/path", 1);

    TempFile temp(R"(
[library]
incdir = ["$SLANG_AUTOS_TEST_INC"]
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->incdirs.has_value());
    CHECK((*config->incdirs)[0] == "/inc/path");

    unsetenv("SLANG_AUTOS_TEST_INC");
}

TEST_CASE("ConfigLoader::loadFile - does not expand env vars in libext", "[config]") {
    setenv("SLANG_AUTOS_TEST_EXT", "expanded", 1);

    TempFile temp(R"(
[library]
libext = ["$SLANG_AUTOS_TEST_EXT", ".sv"]
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    REQUIRE(config->libext.has_value());
    CHECK((*config->libext)[0] == "$SLANG_AUTOS_TEST_EXT");
    CHECK((*config->libext)[1] == ".sv");

    unsetenv("SLANG_AUTOS_TEST_EXT");
}

// ============================================================================
// TOML Grouping Parsing
// ============================================================================

TEST_CASE("ConfigLoader::loadFile - parses grouping", "[config]") {
    SECTION("alphabetical") {
        TempFile temp("[formatting]\ngrouping = \"alphabetical\"\n");
        auto config = ConfigLoader::loadFile(temp.file());
        REQUIRE(config.has_value());
        REQUIRE(config->grouping.has_value());
        CHECK(*config->grouping == PortGrouping::Alphabetical);
    }

    SECTION("alpha (alias)") {
        TempFile temp("[formatting]\ngrouping = \"alpha\"\n");
        auto config = ConfigLoader::loadFile(temp.file());
        REQUIRE(config.has_value());
        REQUIRE(config->grouping.has_value());
        CHECK(*config->grouping == PortGrouping::Alphabetical);
    }

    SECTION("direction") {
        TempFile temp("[formatting]\ngrouping = \"direction\"\n");
        auto config = ConfigLoader::loadFile(temp.file());
        REQUIRE(config.has_value());
        REQUIRE(config->grouping.has_value());
        CHECK(*config->grouping == PortGrouping::ByDirection);
    }

    SECTION("bydirection (alias)") {
        TempFile temp("[formatting]\ngrouping = \"bydirection\"\n");
        auto config = ConfigLoader::loadFile(temp.file());
        REQUIRE(config.has_value());
        REQUIRE(config->grouping.has_value());
        CHECK(*config->grouping == PortGrouping::ByDirection);
    }

    SECTION("declaration") {
        TempFile temp("[formatting]\ngrouping = \"declaration\"\n");
        auto config = ConfigLoader::loadFile(temp.file());
        REQUIRE(config.has_value());
        REQUIRE(config->grouping.has_value());
        CHECK(*config->grouping == PortGrouping::ByDeclaration);
    }

    SECTION("bydeclaration (alias)") {
        TempFile temp("[formatting]\ngrouping = \"bydeclaration\"\n");
        auto config = ConfigLoader::loadFile(temp.file());
        REQUIRE(config.has_value());
        REQUIRE(config->grouping.has_value());
        CHECK(*config->grouping == PortGrouping::ByDeclaration);
    }

    SECTION("invalid value warns") {
        TempFile temp("[formatting]\ngrouping = \"invalid\"\n");
        DiagnosticCollector diag;
        auto config = ConfigLoader::loadFile(temp.file(), &diag);
        REQUIRE(config.has_value());
        CHECK_FALSE(config->grouping.has_value());
        CHECK(diag.warningCount() > 0);
    }
}

// ============================================================================
// Inline verbosity and single-unit Parsing
// ============================================================================

TEST_CASE("parseInlineConfig - parses verbosity", "[config]") {
    SECTION("valid values") {
        auto config = parseInlineConfig("// slang-autos-verbosity: 0\n");
        REQUIRE(config.verbosity.has_value());
        CHECK(*config.verbosity == 0);

        config = parseInlineConfig("// slang-autos-verbosity: 2\n");
        REQUIRE(config.verbosity.has_value());
        CHECK(*config.verbosity == 2);
    }

    SECTION("out of range warns") {
        DiagnosticCollector diag;
        auto config = parseInlineConfig("// slang-autos-verbosity: 5\n", "", &diag);
        CHECK_FALSE(config.verbosity.has_value());
        CHECK(diag.warningCount() > 0);
    }

    SECTION("non-numeric warns") {
        DiagnosticCollector diag;
        auto config = parseInlineConfig("// slang-autos-verbosity: high\n", "", &diag);
        CHECK_FALSE(config.verbosity.has_value());
        CHECK(diag.warningCount() > 0);
    }
}

TEST_CASE("parseInlineConfig - parses single-unit", "[config]") {
    SECTION("true values") {
        for (const auto& val : {"true", "1", "yes"}) {
            auto config = parseInlineConfig(
                std::string("// slang-autos-single-unit: ") + val + "\n");
            REQUIRE(config.single_unit.has_value());
            CHECK(*config.single_unit == true);
        }
    }

    SECTION("false values") {
        for (const auto& val : {"false", "0", "no"}) {
            auto config = parseInlineConfig(
                std::string("// slang-autos-single-unit: ") + val + "\n");
            REQUIRE(config.single_unit.has_value());
            CHECK(*config.single_unit == false);
        }
    }

    SECTION("invalid value warns") {
        DiagnosticCollector diag;
        auto config = parseInlineConfig("// slang-autos-single-unit: maybe\n", "", &diag);
        CHECK_FALSE(config.single_unit.has_value());
        CHECK(diag.warningCount() > 0);
    }
}

// ============================================================================
// Config Parity Tests
// ============================================================================
// Every setting should be configurable from both TOML and inline comments,
// and the merge should respect the priority chain: CLI > inline > TOML > defaults.

TEST_CASE("Config parity - grouping flows through TOML to tool options", "[config][parity]") {
    TempFile temp("[formatting]\ngrouping = \"alphabetical\"\n");
    auto file_config = ConfigLoader::loadFile(temp.file());

    InlineConfig inline_cfg;
    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_config, inline_cfg, cli_opts, cli_flags);

    REQUIRE(merged.grouping.has_value());
    CHECK(*merged.grouping == PortGrouping::Alphabetical);

    auto tool_opts = merged.toToolOptions();
    REQUIRE(tool_opts.grouping.has_value());
    CHECK(*tool_opts.grouping == PortGrouping::Alphabetical);
}

TEST_CASE("Config parity - inline grouping overrides TOML grouping", "[config][parity]") {
    FileConfig file_cfg;
    file_cfg.grouping = PortGrouping::Alphabetical;

    InlineConfig inline_cfg;
    inline_cfg.grouping = PortGrouping::ByDeclaration;

    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    REQUIRE(merged.grouping.has_value());
    CHECK(*merged.grouping == PortGrouping::ByDeclaration);
}

TEST_CASE("Config parity - inline verbosity overrides TOML verbosity", "[config][parity]") {
    FileConfig file_cfg;
    file_cfg.verbosity = 0;

    InlineConfig inline_cfg;
    inline_cfg.verbosity = 2;

    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.verbosity == 2);
}

TEST_CASE("Config parity - inline single_unit overrides TOML single_unit", "[config][parity]") {
    FileConfig file_cfg;
    file_cfg.single_unit = true;

    InlineConfig inline_cfg;
    inline_cfg.single_unit = false;

    AutosToolOptions cli_opts;
    CliFlags cli_flags;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.single_unit == false);
}

TEST_CASE("Config parity - CLI overrides inline verbosity", "[config][parity]") {
    FileConfig file_cfg;

    InlineConfig inline_cfg;
    inline_cfg.verbosity = 2;

    AutosToolOptions cli_opts;
    cli_opts.verbosity = 0;
    CliFlags cli_flags;
    cli_flags.has_verbosity = true;

    auto merged = ConfigLoader::merge(file_cfg, inline_cfg, cli_opts, cli_flags);

    CHECK(merged.verbosity == 0);
}

TEST_CASE("Config parity - all settings round-trip through TOML", "[config][parity]") {
    TempFile temp(R"(
[library]
libdir = ["./lib"]
libext = [".v", ".sv"]
incdir = ["./inc"]

[formatting]
indent = 4
alignment = false
grouping = "declaration"
direction_comments = true

[behavior]
strictness = "strict"
verbosity = 2
single_unit = false
resolved_ranges = true
)");

    auto config = ConfigLoader::loadFile(temp.file());

    REQUIRE(config.has_value());
    CHECK(config->libdirs.has_value());
    CHECK(config->libext.has_value());
    CHECK(config->incdirs.has_value());
    CHECK(config->indent.has_value());
    CHECK(config->alignment.has_value());
    CHECK(config->grouping.has_value());
    CHECK(config->direction_comments.has_value());
    CHECK(config->strictness.has_value());
    CHECK(config->verbosity.has_value());
    CHECK(config->single_unit.has_value());
    CHECK(config->resolved_ranges.has_value());

    CHECK(*config->indent == 4);
    CHECK(*config->alignment == false);
    CHECK(*config->grouping == PortGrouping::ByDeclaration);
    CHECK(*config->strictness == StrictnessMode::Strict);
    CHECK(*config->verbosity == 2);
    CHECK(*config->single_unit == false);
    CHECK(*config->resolved_ranges == true);
}

// ============================================================================
// Unknown Key Warning Tests
// ============================================================================

TEST_CASE("ConfigLoader::loadFile - warns on unknown top-level section", "[config]") {
    TempFile temp("[bogus]\nfoo = 1\n");
    DiagnosticCollector diag;
    auto config = ConfigLoader::loadFile(temp.file(), &diag);
    REQUIRE(config.has_value());
    CHECK(diag.warningCount() > 0);
    CHECK(diag.diagnostics()[0].message.find("bogus") != std::string::npos);
}

TEST_CASE("ConfigLoader::loadFile - warns on unknown library key", "[config]") {
    TempFile temp("[library]\nlibdirs = [\"./lib\"]\n");  // should be "libdir"
    DiagnosticCollector diag;
    auto config = ConfigLoader::loadFile(temp.file(), &diag);
    REQUIRE(config.has_value());
    CHECK_FALSE(config->libdirs.has_value());
    REQUIRE(diag.warningCount() > 0);
    CHECK(diag.diagnostics()[0].message.find("libdirs") != std::string::npos);
}

TEST_CASE("ConfigLoader::loadFile - warns on unknown formatting key", "[config]") {
    TempFile temp("[formatting]\nalign = true\n");  // should be "alignment"
    DiagnosticCollector diag;
    auto config = ConfigLoader::loadFile(temp.file(), &diag);
    REQUIRE(config.has_value());
    CHECK_FALSE(config->alignment.has_value());
    REQUIRE(diag.warningCount() > 0);
    CHECK(diag.diagnostics()[0].message.find("align") != std::string::npos);
}

TEST_CASE("ConfigLoader::loadFile - warns on unknown behavior key", "[config]") {
    TempFile temp("[behavior]\nstrict = true\n");  // should be "strictness"
    DiagnosticCollector diag;
    auto config = ConfigLoader::loadFile(temp.file(), &diag);
    REQUIRE(config.has_value());
    CHECK_FALSE(config->strictness.has_value());
    REQUIRE(diag.warningCount() > 0);
    CHECK(diag.diagnostics()[0].message.find("strict") != std::string::npos);
}

TEST_CASE("ConfigLoader::loadFile - no warnings on valid config", "[config]") {
    TempFile temp(R"(
[library]
libdir = ["./lib"]
libext = [".v"]
incdir = ["./inc"]

[formatting]
indent = 2
alignment = true
grouping = "direction"
direction_comments = true

[behavior]
strictness = "lenient"
verbosity = 1
single_unit = true
resolved_ranges = false
)");
    DiagnosticCollector diag;
    auto config = ConfigLoader::loadFile(temp.file(), &diag);
    REQUIRE(config.has_value());
    CHECK(diag.warningCount() == 0);
}

TEST_CASE("Config parity - all settings round-trip through inline", "[config][parity]") {
    std::string content = R"(
module test;
endmodule
// slang-autos-libdir: ./lib
// slang-autos-libext: .v .sv
// slang-autos-incdir: ./inc
// slang-autos-indent: 4
// slang-autos-alignment: false
// slang-autos-grouping: declaration
// slang-autos-direction-comments: true
// slang-autos-strictness: strict
// slang-autos-verbosity: 2
// slang-autos-single-unit: false
// slang-autos-resolved-ranges: true
)";

    auto config = parseInlineConfig(content);

    CHECK(config.libdirs.size() == 1);
    CHECK(config.libext.size() == 2);
    CHECK(config.incdirs.size() == 1);
    REQUIRE(config.indent.has_value());
    CHECK(*config.indent == 4);
    REQUIRE(config.alignment.has_value());
    CHECK(*config.alignment == false);
    REQUIRE(config.grouping.has_value());
    CHECK(*config.grouping == PortGrouping::ByDeclaration);
    REQUIRE(config.direction_comments.has_value());
    REQUIRE(config.strictness.has_value());
    CHECK(*config.strictness == StrictnessMode::Strict);
    REQUIRE(config.verbosity.has_value());
    CHECK(*config.verbosity == 2);
    REQUIRE(config.single_unit.has_value());
    CHECK(*config.single_unit == false);
    REQUIRE(config.resolved_ranges.has_value());
    CHECK(*config.resolved_ranges == true);
}
