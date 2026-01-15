// Unit tests for SignalAggregator functions

#include <catch2/catch_test_macros.hpp>
#include "slang-autos/SignalAggregator.h"

using namespace slang_autos;

// ============================================================================
// isVerilogConstant tests
// ============================================================================

TEST_CASE("isVerilogConstant - unsized literals", "[signal_aggregator]") {
    CHECK(isVerilogConstant("'0"));
    CHECK(isVerilogConstant("'1"));
    CHECK(isVerilogConstant("'z"));
    CHECK(isVerilogConstant("'x"));
    CHECK(isVerilogConstant("'Z"));
    CHECK(isVerilogConstant("'X"));
}

TEST_CASE("isVerilogConstant - sized binary literals", "[signal_aggregator]") {
    CHECK(isVerilogConstant("1'b0"));
    CHECK(isVerilogConstant("1'b1"));
    CHECK(isVerilogConstant("8'b10101010"));
    CHECK(isVerilogConstant("4'bxxxx"));
    CHECK(isVerilogConstant("4'bzzzz"));
    CHECK(isVerilogConstant("8'b1010_1010"));
}

TEST_CASE("isVerilogConstant - sized hex literals", "[signal_aggregator]") {
    CHECK(isVerilogConstant("8'hFF"));
    CHECK(isVerilogConstant("8'hff"));
    CHECK(isVerilogConstant("32'hDEAD_BEEF"));
    CHECK(isVerilogConstant("4'hx"));
}

TEST_CASE("isVerilogConstant - sized decimal literals", "[signal_aggregator]") {
    CHECK(isVerilogConstant("8'd255"));
    CHECK(isVerilogConstant("32'd100"));
}

TEST_CASE("isVerilogConstant - sized octal literals", "[signal_aggregator]") {
    CHECK(isVerilogConstant("8'o377"));
    CHECK(isVerilogConstant("3'o7"));
}

TEST_CASE("isVerilogConstant - non-constants", "[signal_aggregator]") {
    CHECK_FALSE(isVerilogConstant("sig_a"));
    CHECK_FALSE(isVerilogConstant("clk"));
    CHECK_FALSE(isVerilogConstant("data_out"));
    CHECK_FALSE(isVerilogConstant("sig_a[7:0]"));
    CHECK_FALSE(isVerilogConstant(""));
}

// ============================================================================
// extractIdentifiers tests
// ============================================================================

TEST_CASE("extractIdentifiers - simple identifier", "[signal_aggregator]") {
    auto result = extractIdentifiers("sig_a");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "sig_a");
}

TEST_CASE("extractIdentifiers - identifier with bit select", "[signal_aggregator]") {
    auto result = extractIdentifiers("sig_a[7:0]");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "sig_a");
}

TEST_CASE("extractIdentifiers - identifier with part select", "[signal_aggregator]") {
    auto result = extractIdentifiers("data[15:8]");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "data");
}

TEST_CASE("extractIdentifiers - constant returns empty", "[signal_aggregator]") {
    CHECK(extractIdentifiers("1'b0").empty());
    CHECK(extractIdentifiers("8'hFF").empty());
    CHECK(extractIdentifiers("'0").empty());
}

TEST_CASE("extractIdentifiers - simple concatenation", "[signal_aggregator]") {
    auto result = extractIdentifiers("{sig_a, sig_b}");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "sig_a");
    CHECK(result[1] == "sig_b");
}

TEST_CASE("extractIdentifiers - concatenation with constant", "[signal_aggregator]") {
    // This is the key bug case: {1'b0, sig_a} should return just ["sig_a"]
    auto result = extractIdentifiers("{1'b0, sig_a}");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "sig_a");
}

TEST_CASE("extractIdentifiers - concatenation with multiple constants", "[signal_aggregator]") {
    auto result = extractIdentifiers("{1'b0, sig_a, 2'b00}");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "sig_a");
}

TEST_CASE("extractIdentifiers - concatenation with bit selects", "[signal_aggregator]") {
    auto result = extractIdentifiers("{sig_a[7:0], sig_b[3:0]}");
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "sig_a");
    CHECK(result[1] == "sig_b");
}

TEST_CASE("extractIdentifiers - nested concatenation", "[signal_aggregator]") {
    auto result = extractIdentifiers("{{sig_a, sig_b}, sig_c}");
    REQUIRE(result.size() == 3);
    CHECK(result[0] == "sig_a");
    CHECK(result[1] == "sig_b");
    CHECK(result[2] == "sig_c");
}

TEST_CASE("extractIdentifiers - whitespace handling", "[signal_aggregator]") {
    auto result = extractIdentifiers("  { 1'b0 , sig_a }  ");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "sig_a");
}

TEST_CASE("extractIdentifiers - empty string", "[signal_aggregator]") {
    CHECK(extractIdentifiers("").empty());
    CHECK(extractIdentifiers("   ").empty());
}

// ============================================================================
// isConcatenation tests
// ============================================================================

TEST_CASE("isConcatenation - simple concatenation", "[signal_aggregator]") {
    CHECK(isConcatenation("{sig_a, sig_b}"));
    CHECK(isConcatenation("{1'b0, sig_a}"));
    CHECK(isConcatenation("{ sig_a , sig_b }"));
}

TEST_CASE("isConcatenation - not a concatenation", "[signal_aggregator]") {
    CHECK_FALSE(isConcatenation("signal"));
    CHECK_FALSE(isConcatenation("sig_a[7:0]"));
    CHECK_FALSE(isConcatenation("1'b0"));
    CHECK_FALSE(isConcatenation(""));
}

TEST_CASE("isConcatenation - complex cases", "[signal_aggregator]") {
    CHECK(isConcatenation("{{sig_a, sig_b}, sig_c}"));
    CHECK(isConcatenation("{sig_a[7:0], sig_b[3:0]}"));
}
