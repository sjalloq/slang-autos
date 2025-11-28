#include <catch2/catch_test_macros.hpp>

#include "slang-autos/Expander.h"
#include "slang-autos/Diagnostics.h"

using namespace slang_autos;

TEST_CASE("AutoInstExpander - basic expansion", "[expander]") {
    std::vector<PortInfo> ports = {
        {"clk", "input", 1},
        {"rst_n", "input", 1},
        {"data_in", "input", 8},
        {"data_out", "output", 8},
        {"valid", "output", 1}
    };
    ports[2].range_str = "[7:0]";
    ports[3].range_str = "[7:0]";

    AutoInstExpander expander;
    std::string result = expander.expand("u_sub", ports, {}, "", "    ", true);

    CHECK(result.find(".clk") != std::string::npos);
    CHECK(result.find(".data_in") != std::string::npos);
    CHECK(result.find(".data_out") != std::string::npos);
    CHECK(result.find("// Outputs") != std::string::npos);
    CHECK(result.find("// Inputs") != std::string::npos);
}

TEST_CASE("AutoInstExpander - skip manual ports", "[expander]") {
    std::vector<PortInfo> ports = {
        {"clk", "input", 1},
        {"data_in", "input", 8},
        {"data_out", "output", 8}
    };

    std::set<std::string> manual_ports = {"clk"};

    AutoInstExpander expander;
    std::string result = expander.expand("u_sub", ports, manual_ports, "", "    ", true);

    CHECK(result.find(".clk") == std::string::npos);  // Should be skipped
    CHECK(result.find(".data_in") != std::string::npos);
    CHECK(result.find(".data_out") != std::string::npos);
}

TEST_CASE("AutoInstExpander - with template", "[expander]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.rules.emplace_back("data_in", "my_input");
    tmpl.rules.emplace_back("data_out", "my_output");

    std::vector<PortInfo> ports = {
        {"data_in", "input", 8},
        {"data_out", "output", 8}
    };

    AutoInstExpander expander(&tmpl);
    std::string result = expander.expand("u_sub", ports, {}, "", "    ", true);

    CHECK(result.find("(my_input)") != std::string::npos);
    CHECK(result.find("(my_output)") != std::string::npos);
}

TEST_CASE("AutoInstExpander - filter pattern", "[expander]") {
    std::vector<PortInfo> ports = {
        {"clk", "input", 1},
        {"data_in", "input", 8},
        {"data_out", "output", 8}
    };

    AutoInstExpander expander;
    std::string result = expander.expand("u_sub", ports, {}, "data_.*", "    ", true);

    CHECK(result.find(".clk") == std::string::npos);  // Filtered out
    CHECK(result.find(".data_in") != std::string::npos);
    CHECK(result.find(".data_out") != std::string::npos);
}

TEST_CASE("AutoInstExpander - getExpandedSignals", "[expander]") {
    std::vector<PortInfo> ports = {
        {"clk", "input", 1},
        {"data_out", "output", 8}
    };
    ports[1].range_str = "[7:0]";

    AutoInstExpander expander;
    auto signals = expander.getExpandedSignals("u_sub", ports);

    REQUIRE(signals.size() == 2);

    // Find the data_out signal
    auto it = std::find_if(signals.begin(), signals.end(),
        [](const ExpandedSignal& s) { return s.signal_name == "data_out"; });
    REQUIRE(it != signals.end());
    CHECK(it->direction == "output");
    CHECK(it->range_str == "[7:0]");
}

TEST_CASE("AutoWireExpander - basic expansion", "[expander]") {
    std::vector<ExpandedSignal> signals = {
        {"wire_a", "output", "[7:0]", "[WIDTH-1:0]"},
        {"wire_b", "output", "", ""},
        {"input_sig", "input", "[3:0]", ""}  // Should be skipped (input)
    };

    AutoWireExpander expander;
    std::string result = expander.expand(signals, {}, "    ");

    CHECK(result.find("wire [WIDTH-1:0] wire_a;") != std::string::npos);
    CHECK(result.find("wire wire_b;") != std::string::npos);
    CHECK(result.find("input_sig") == std::string::npos);  // Skipped
    CHECK(result.find("// Beginning of automatic wires") != std::string::npos);
    CHECK(result.find("// End of automatic wires") != std::string::npos);
}

TEST_CASE("AutoWireExpander - skip existing declarations", "[expander]") {
    std::vector<ExpandedSignal> signals = {
        {"wire_a", "output", "", ""},
        {"wire_b", "output", "", ""}
    };

    std::set<std::string> existing = {"wire_a"};

    AutoWireExpander expander;
    std::string result = expander.expand(signals, existing, "    ");

    CHECK(result.find("wire_a") == std::string::npos);  // Skipped
    CHECK(result.find("wire wire_b;") != std::string::npos);
}
