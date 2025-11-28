#include <catch2/catch_test_macros.hpp>

#include "slang-autos/Tool.h"

using namespace slang_autos;

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
