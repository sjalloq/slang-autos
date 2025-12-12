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
