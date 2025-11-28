#include <catch2/catch_test_macros.hpp>

#include "slang-autos/Expander.h"
#include "slang-autos/Diagnostics.h"
#include "slang-autos/Parser.h"

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
    CHECK(result.find("// End of automatics") != std::string::npos);
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

// ============================================================================
// SignalAggregator Tests
// ============================================================================

TEST_CASE("SignalAggregator - basic net collection", "[aggregator]") {
    SignalAggregator aggregator;

    std::vector<PortConnection> connections = {
        {"data_out", "data_out", "output"},
        {"data_in", "data_in", "input"},
        {"clk", "clk", "input"}
    };

    std::vector<PortInfo> ports = {
        {"data_out", "output", 8},
        {"data_in", "input", 8},
        {"clk", "input", 1}
    };
    ports[0].range_str = "[7:0]";
    ports[1].range_str = "[7:0]";

    aggregator.addFromInstance("u_inst", connections, ports);

    // Check instance-driven nets (outputs)
    auto driven_nets = aggregator.getInstanceDrivenNets();
    CHECK(driven_nets.size() == 1);
    CHECK(driven_nets[0].name == "data_out");
    CHECK(driven_nets[0].width == 8);

    // Check external inputs (inputs not driven by instances)
    auto external_inputs = aggregator.getExternalInputNets();
    CHECK(external_inputs.size() == 2);  // data_in and clk

    // Check external outputs (outputs not consumed by instances)
    auto external_outputs = aggregator.getExternalOutputNets();
    CHECK(external_outputs.size() == 1);  // data_out (not consumed by any input)
}

TEST_CASE("SignalAggregator - width merging", "[aggregator]") {
    SignalAggregator aggregator;

    // First instance uses 8-bit width
    std::vector<PortConnection> conn1 = {{"sig", "sig", "output"}};
    std::vector<PortInfo> ports1 = {{"sig", "output", 8}};

    // Second instance uses 16-bit width
    std::vector<PortConnection> conn2 = {{"sig", "sig", "output"}};
    std::vector<PortInfo> ports2 = {{"sig", "output", 16}};

    aggregator.addFromInstance("u_inst1", conn1, ports1);
    aggregator.addFromInstance("u_inst2", conn2, ports2);

    // Should take the maximum width
    auto width = aggregator.getNetWidth("sig");
    REQUIRE(width.has_value());
    CHECK(*width == 16);
}

TEST_CASE("SignalAggregator - inout handling", "[aggregator]") {
    SignalAggregator aggregator;

    std::vector<PortConnection> connections = {
        {"bidir", "bidir", "inout"}
    };
    std::vector<PortInfo> ports = {
        {"bidir", "inout", 8}
    };

    aggregator.addFromInstance("u_inst", connections, ports);

    // Inout should appear in inout nets
    auto inouts = aggregator.getInoutNets();
    CHECK(inouts.size() == 1);
    CHECK(inouts[0].name == "bidir");

    // Should be marked as driven by instance
    CHECK(aggregator.isDrivenByInstance("bidir"));
}

TEST_CASE("SignalAggregator - unconnected and constants skipped", "[aggregator]") {
    SignalAggregator aggregator;

    std::vector<PortConnection> connections = {
        {"normal", "normal", "output"},
        {"unused", "", "output"},  // unconnected
        {"const", "'0", "input"}   // constant
    };
    connections[1].is_unconnected = true;
    connections[2].is_constant = true;

    std::vector<PortInfo> ports = {
        {"normal", "output", 1},
        {"unused", "output", 1},
        {"const", "input", 1}
    };

    aggregator.addFromInstance("u_inst", connections, ports);

    // Only normal should be tracked
    auto driven = aggregator.getInstanceDrivenNetNames();
    CHECK(driven.size() == 1);
    CHECK(driven.count("normal") == 1);
    CHECK(driven.count("unused") == 0);
}

// ============================================================================
// AutoWireExpander with Aggregator Tests
// ============================================================================

TEST_CASE("AutoWireExpander - expandFromAggregator", "[expander]") {
    SignalAggregator aggregator;

    std::vector<PortConnection> connections = {
        {"out_a", "out_a", "output"},
        {"out_b", "out_b", "output"},
        {"in_c", "in_c", "input"}
    };
    std::vector<PortInfo> ports = {
        {"out_a", "output", 8},
        {"out_b", "output", 1},
        {"in_c", "input", 4}
    };

    aggregator.addFromInstance("u_inst", connections, ports);

    std::set<std::string> existing = {"out_a"};  // Already declared

    AutoWireExpander expander;
    std::string result = expander.expandFromAggregator(
        aggregator, existing, "logic", "    ", PortGrouping::ByDirection);

    CHECK(result.find("out_a") == std::string::npos);  // Already declared
    CHECK(result.find("logic out_b") != std::string::npos);
    CHECK(result.find("in_c") == std::string::npos);  // Input, not driven
}

// ============================================================================
// AutoRegExpander Tests
// ============================================================================

TEST_CASE("AutoRegExpander - basic expansion", "[expander]") {
    SignalAggregator aggregator;

    // Instance drives internal_sig
    std::vector<PortConnection> connections = {
        {"internal_sig", "internal_sig", "output"}
    };
    std::vector<PortInfo> ports = {
        {"internal_sig", "output", 8}
    };
    aggregator.addFromInstance("u_inst", connections, ports);

    // Module has outputs: internal_sig and external_reg
    std::vector<NetInfo> module_outputs = {
        NetInfo("internal_sig", 8),
        NetInfo("external_reg", 16)
    };

    std::set<std::string> existing;

    AutoRegExpander expander;
    std::string result = expander.expand(
        module_outputs, aggregator, existing, "logic", "    ", PortGrouping::ByDirection);

    // internal_sig is driven by instance, should NOT be in AUTOREG
    CHECK(result.find("internal_sig") == std::string::npos);
    // external_reg is not driven by instance, should be in AUTOREG
    CHECK(result.find("logic [15:0] external_reg") != std::string::npos);
}

TEST_CASE("AutoRegExpander - skip existing declarations", "[expander]") {
    SignalAggregator aggregator;  // Empty - no instances

    std::vector<NetInfo> module_outputs = {
        NetInfo("reg_a", 8),
        NetInfo("reg_b", 8)
    };

    std::set<std::string> existing = {"reg_a"};

    AutoRegExpander expander;
    std::string result = expander.expand(
        module_outputs, aggregator, existing, "logic", "    ", PortGrouping::ByDirection);

    CHECK(result.find("reg_a") == std::string::npos);  // Already declared
    CHECK(result.find("logic [7:0] reg_b") != std::string::npos);
}

// ============================================================================
// AutoPortsExpander Tests
// ============================================================================

TEST_CASE("AutoPortsExpander - basic expansion", "[expander]") {
    SignalAggregator aggregator;

    // Instance connections
    std::vector<PortConnection> connections = {
        {"out_data", "out_data", "output"},  // External output (not consumed)
        {"in_data", "in_data", "input"}      // External input (not driven)
    };
    std::vector<PortInfo> ports = {
        {"out_data", "output", 8},
        {"in_data", "input", 8}
    };
    aggregator.addFromInstance("u_inst", connections, ports);

    std::set<std::string> existing;

    AutoPortsExpander expander;
    std::string result = expander.expand(
        aggregator, existing, "logic", "    ", PortGrouping::ByDirection);

    // Should generate ANSI-style port declarations
    CHECK(result.find("output logic [7:0] out_data") != std::string::npos);
    CHECK(result.find("input logic [7:0] in_data") != std::string::npos);
}

TEST_CASE("AutoPortsExpander - skip existing ports", "[expander]") {
    SignalAggregator aggregator;

    std::vector<PortConnection> connections = {
        {"port_a", "port_a", "input"},
        {"port_b", "port_b", "input"}
    };
    std::vector<PortInfo> ports = {
        {"port_a", "input", 1},
        {"port_b", "input", 1}
    };
    aggregator.addFromInstance("u_inst", connections, ports);

    std::set<std::string> existing = {"port_a"};

    AutoPortsExpander expander;
    std::string result = expander.expand(
        aggregator, existing, "logic", "    ", PortGrouping::ByDirection);

    CHECK(result.find("port_a") == std::string::npos);  // Already declared
    CHECK(result.find("input logic port_b") != std::string::npos);
}

TEST_CASE("AutoPortsExpander - alphabetical grouping", "[expander]") {
    SignalAggregator aggregator;

    std::vector<PortConnection> connections = {
        {"zebra", "zebra", "input"},
        {"alpha", "alpha", "output"}
    };
    std::vector<PortInfo> ports = {
        {"zebra", "input", 1},
        {"alpha", "output", 1}
    };
    aggregator.addFromInstance("u_inst", connections, ports);

    std::set<std::string> existing;

    AutoPortsExpander expander;
    std::string result = expander.expand(
        aggregator, existing, "logic", "    ", PortGrouping::Alphabetical);

    // In alphabetical order, alpha should come before zebra
    size_t alpha_pos = result.find("alpha");
    size_t zebra_pos = result.find("zebra");
    CHECK(alpha_pos < zebra_pos);
}
