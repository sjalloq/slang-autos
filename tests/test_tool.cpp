#include <catch2/catch_test_macros.hpp>

#include "slang-autos/Tool.h"
#include "slang-autos/Constants.h"

using namespace slang_autos;

TEST_CASE("Constants marker strings", "[constants]") {
    SECTION("AUTO markers are defined correctly") {
        CHECK(markers::AUTOINST == "/*AUTOINST*/");
        CHECK(markers::AUTOLOGIC == "/*AUTOLOGIC*/");
        CHECK(markers::AUTOPORTS == "/*AUTOPORTS*/");
    }

    SECTION("Block delimiters are defined correctly") {
        CHECK(markers::BEGIN_AUTOLOGIC == "// Beginning of automatic logic");
        CHECK(markers::END_AUTOMATICS == "// End of automatics");
    }
}

TEST_CASE("AutosTool - loadWithArgs basic", "[tool]") {
    AutosTool tool;

    SECTION("Empty args") {
        bool result = tool.loadWithArgs({});
        CHECK_FALSE(result);
    }

    SECTION("Non-existent file") {
        bool result = tool.loadWithArgs({"nonexistent.sv"});
        CHECK_FALSE(result);
    }
}

TEST_CASE("AutosTool - loadWithArgs with EDA options", "[tool]") {
    AutosTool tool;

    SECTION("With -y library path") {
        bool result = tool.loadWithArgs({"-y", "/tmp"});
        CHECK_FALSE(result);
    }
}
