#include <catch2/catch_test_macros.hpp>

#include "slang-autos/Writer.h"

using namespace slang_autos;

TEST_CASE("SourceWriter - apply single replacement", "[writer]") {
    SourceWriter writer;
    std::string content = "Hello World";

    std::vector<Replacement> repls = {
        {6, 11, "Universe"}
    };

    std::string result = writer.applyReplacements(content, repls);
    CHECK(result == "Hello Universe");
}

TEST_CASE("SourceWriter - apply multiple replacements", "[writer]") {
    SourceWriter writer;
    std::string content = "aaa bbb ccc";

    std::vector<Replacement> repls = {
        {0, 3, "AAA"},
        {4, 7, "BBB"},
        {8, 11, "CCC"}
    };

    std::string result = writer.applyReplacements(content, repls);
    CHECK(result == "AAA BBB CCC");
}

TEST_CASE("SourceWriter - replacements applied bottom-up", "[writer]") {
    SourceWriter writer;
    std::string content = "01234567890";

    // Replacements in random order - should still work
    std::vector<Replacement> repls = {
        {2, 4, "XX"},    // Middle
        {8, 10, "ZZ"},   // End
        {0, 2, "AA"}     // Start
    };

    std::string result = writer.applyReplacements(content, repls);
    CHECK(result == "AAXX4567ZZ0");
}

TEST_CASE("SourceWriter - replacement that shrinks", "[writer]") {
    SourceWriter writer;
    std::string content = "Hello Beautiful World";

    std::vector<Replacement> repls = {
        {6, 16, ""}  // Remove "Beautiful "
    };

    std::string result = writer.applyReplacements(content, repls);
    CHECK(result == "Hello World");
}

TEST_CASE("SourceWriter - replacement that expands", "[writer]") {
    SourceWriter writer;
    std::string content = "AB";

    std::vector<Replacement> repls = {
        {1, 1, "123"}  // Insert between A and B
    };

    std::string result = writer.applyReplacements(content, repls);
    CHECK(result == "A123B");
}

TEST_CASE("findInstanceInfoFromAutoinst - simple instance", "[writer]") {
    std::string content = "submod u_sub (/*AUTOINST*/);";
    size_t autoinst_pos = content.find("/*AUTOINST*/");

    auto result = findInstanceInfoFromAutoinst(content, autoinst_pos);
    REQUIRE(result.has_value());

    auto [module_type, instance_name, start] = *result;
    CHECK(module_type == "submod");
    CHECK(instance_name == "u_sub");
}

TEST_CASE("findInstanceInfoFromAutoinst - with parameters", "[writer]") {
    std::string content = "submod #(.WIDTH(8)) u_sub (/*AUTOINST*/);";
    size_t autoinst_pos = content.find("/*AUTOINST*/");

    auto result = findInstanceInfoFromAutoinst(content, autoinst_pos);
    REQUIRE(result.has_value());

    auto [module_type, instance_name, start] = *result;
    CHECK(module_type == "submod");
    CHECK(instance_name == "u_sub");
}

TEST_CASE("findInstanceInfoFromAutoinst - multiline", "[writer]") {
    std::string content = R"(
        submod u_sub (
            .clk(clk),
            /*AUTOINST*/
        );
    )";
    size_t autoinst_pos = content.find("/*AUTOINST*/");

    auto result = findInstanceInfoFromAutoinst(content, autoinst_pos);
    REQUIRE(result.has_value());

    auto [module_type, instance_name, start] = *result;
    CHECK(module_type == "submod");
    CHECK(instance_name == "u_sub");
}

TEST_CASE("findInstanceCloseParen - simple", "[writer]") {
    std::string content = "/*AUTOINST*/);";
    size_t autoinst_end = content.find("*/") + 2;

    auto result = findInstanceCloseParen(content, autoinst_end);
    REQUIRE(result.has_value());
    CHECK(content[*result] == ')');
}

TEST_CASE("findInstanceCloseParen - with nested parens", "[writer]") {
    std::string content = "/*AUTOINST*/ .port(func(a, b)));";
    size_t autoinst_end = content.find("*/") + 2;

    auto result = findInstanceCloseParen(content, autoinst_end);
    REQUIRE(result.has_value());
    // Should find the final ), not the one in func()
    CHECK(*result == content.length() - 2);
}

TEST_CASE("findInstanceCloseParen - ignores parens in strings", "[writer]") {
    std::string content = R"sv(/*AUTOINST*/ .port("(not this)"));)sv";
    size_t autoinst_end = content.find("*/") + 2;

    auto result = findInstanceCloseParen(content, autoinst_end);
    REQUIRE(result.has_value());
    CHECK(content[*result] == ')');
}

TEST_CASE("findManualPorts", "[writer]") {
    std::string content = R"(
        submod u_sub (
            .clk(my_clk),
            .rst_n(my_rst),
            /*AUTOINST*/
        );
    )";
    size_t autoinst_pos = content.find("/*AUTOINST*/");

    auto manual = findManualPorts(content, autoinst_pos);

    CHECK(manual.count("clk") == 1);
    CHECK(manual.count("rst_n") == 1);
    CHECK(manual.count("nonexistent") == 0);
}

TEST_CASE("findExistingDeclarations", "[writer]") {
    std::string content = R"(
        module top;
            wire clk;
            wire [7:0] data_in, data_out;
            logic [3:0] counter;

            /*AUTOWIRE*/
        endmodule
    )";
    size_t autowire_pos = content.find("/*AUTOWIRE*/");

    auto existing = findExistingDeclarations(content, autowire_pos);

    CHECK(existing.count("clk") == 1);
    CHECK(existing.count("data_in") == 1);
    CHECK(existing.count("data_out") == 1);
    CHECK(existing.count("counter") == 1);
}

TEST_CASE("detectIndent", "[writer]") {
    SECTION("4 spaces") {
        std::string content = "module top;\n    wire clk;\nendmodule";
        size_t offset = content.find("wire");
        CHECK(detectIndent(content, offset) == "    ");
    }

    SECTION("2 spaces") {
        std::string content = "module top;\n  wire clk;\nendmodule";
        size_t offset = content.find("wire");
        CHECK(detectIndent(content, offset) == "  ");
    }

    SECTION("Tab") {
        std::string content = "module top;\n\twire clk;\nendmodule";
        size_t offset = content.find("wire");
        CHECK(detectIndent(content, offset) == "\t");
    }
}

TEST_CASE("offsetToLine", "[writer]") {
    std::string content = "line1\nline2\nline3";

    CHECK(offsetToLine(content, 0) == 1);
    CHECK(offsetToLine(content, 5) == 1);
    CHECK(offsetToLine(content, 6) == 2);
    CHECK(offsetToLine(content, 12) == 3);
}
