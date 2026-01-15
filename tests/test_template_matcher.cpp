#include <catch2/catch_test_macros.hpp>

#include "slang-autos/TemplateMatcher.h"
#include "slang-autos/Diagnostics.h"

using namespace slang_autos;

TEST_CASE("TemplateMatcher - no template", "[template]") {
    TemplateMatcher matcher;

    PortInfo port("data_in", "input", 8);
    auto result = matcher.matchPort(port);

    CHECK(result.signal_name == "data_in");
    CHECK(result.matched_rule == nullptr);
}

TEST_CASE("TemplateMatcher - literal port match", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.rules.emplace_back("data_in", "my_data_in");
    tmpl.rules.emplace_back("data_out", "my_data_out");

    TemplateMatcher matcher(&tmpl);
    matcher.setInstance("u_sub");

    SECTION("Matching port") {
        PortInfo port("data_in", "input", 8);
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "my_data_in");
    }

    SECTION("Non-matching port falls through") {
        PortInfo port("clk", "input", 1);
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "clk");
    }
}

TEST_CASE("TemplateMatcher - port capture groups", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.rules.emplace_back("data_(.*)", "sig_$1");

    TemplateMatcher matcher(&tmpl);
    matcher.setInstance("u_sub");

    PortInfo port("data_in", "input", 8);
    auto result = matcher.matchPort(port);

    CHECK(result.signal_name == "sig_in");
}

TEST_CASE("TemplateMatcher - instance capture groups", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.instance_pattern = "u_sub_(\\d+)";
    tmpl.rules.emplace_back("data", "data_%1");

    TemplateMatcher matcher(&tmpl);
    matcher.setInstance("u_sub_0");

    PortInfo port("data", "input", 8);
    auto result = matcher.matchPort(port);

    CHECK(result.signal_name == "data_0");
}

TEST_CASE("TemplateMatcher - builtin variables", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";

    SECTION("port.name") {
        tmpl.rules.emplace_back(".*", "sig_port.name");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub");

        PortInfo port("data", "input", 8);
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "sig_data");
    }

    SECTION("port.width") {
        tmpl.rules.emplace_back(".*", "sig_w_port.width");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub");

        PortInfo port("data", "input", 8);
        port.width = 8;
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "sig_w_8");
    }

    SECTION("inst.name") {
        tmpl.rules.emplace_back(".*", "inst.name_data");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_0");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "u_sub_0_data");
    }
}

TEST_CASE("TemplateMatcher - special values", "[template]") {
    // Shorthand forms (preferred in templates)
    CHECK(TemplateMatcher::isSpecialValue("_"));
    CHECK(TemplateMatcher::isSpecialValue("0"));
    CHECK(TemplateMatcher::isSpecialValue("1"));
    CHECK(TemplateMatcher::isSpecialValue("z"));
    CHECK(TemplateMatcher::isSpecialValue("Z"));
    CHECK(TemplateMatcher::isSpecialValue("x"));
    CHECK(TemplateMatcher::isSpecialValue("X"));

    // Quoted forms also accepted
    CHECK(TemplateMatcher::isSpecialValue("'0"));
    CHECK(TemplateMatcher::isSpecialValue("'1"));
    CHECK(TemplateMatcher::isSpecialValue("'z"));
    CHECK(TemplateMatcher::isSpecialValue("'x"));

    CHECK_FALSE(TemplateMatcher::isSpecialValue("signal"));

    // Shorthand forms expand to unsized literals
    CHECK(TemplateMatcher::formatSpecialValue("_") == "");
    CHECK(TemplateMatcher::formatSpecialValue("0") == "'0");
    CHECK(TemplateMatcher::formatSpecialValue("1") == "'1");
    CHECK(TemplateMatcher::formatSpecialValue("z") == "'z");
    CHECK(TemplateMatcher::formatSpecialValue("Z") == "'z");
    CHECK(TemplateMatcher::formatSpecialValue("x") == "'x");
    CHECK(TemplateMatcher::formatSpecialValue("X") == "'x");

    // Quoted forms pass through
    CHECK(TemplateMatcher::formatSpecialValue("'0") == "'0");
    CHECK(TemplateMatcher::formatSpecialValue("'1") == "'1");
    CHECK(TemplateMatcher::formatSpecialValue("'z") == "'z");
}

TEST_CASE("TemplateMatcher - port.input/output/inout variables", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.rules.emplace_back(".*", "port.input_port.output_port.inout");

    TemplateMatcher matcher(&tmpl);
    matcher.setInstance("u_sub");

    SECTION("input port") {
        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "1_0_0");
    }

    SECTION("output port") {
        PortInfo port("data", "output");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "0_1_0");
    }

    SECTION("inout port") {
        PortInfo port("data", "inout");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "0_0_1");
    }
}

TEST_CASE("TemplateMatcher - ternary expressions", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";

    SECTION("ternary with constants") {
        tmpl.rules.emplace_back(".*", "port.input ? '0 : _");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub");

        // Input port should get '0
        PortInfo input_port("data_in", "input");
        auto result1 = matcher.matchPort(input_port);
        CHECK(result1.signal_name == "'0");

        // Output port should get _ (unconnected)
        PortInfo output_port("data_out", "output");
        auto result2 = matcher.matchPort(output_port);
        CHECK(result2.signal_name == "_");
    }

    SECTION("ternary with signal names") {
        tmpl.rules.emplace_back(".*", "port.output ? data_out_sig : _");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub");

        // Output port should get signal name
        PortInfo output_port("valid", "output");
        auto result1 = matcher.matchPort(output_port);
        CHECK(result1.signal_name == "data_out_sig");

        // Input port should get _
        PortInfo input_port("ready", "input");
        auto result2 = matcher.matchPort(input_port);
        CHECK(result2.signal_name == "_");
    }

    SECTION("ternary with instance substitution") {
        tmpl.rules.emplace_back("data", "port.input ? data_@_in : data_@_out");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_3");

        PortInfo input_port("data", "input");
        auto result1 = matcher.matchPort(input_port);
        CHECK(result1.signal_name == "data_3_in");

        PortInfo output_port("data", "output");
        auto result2 = matcher.matchPort(output_port);
        CHECK(result2.signal_name == "data_3_out");
    }

    SECTION("non-ternary expression unchanged") {
        tmpl.rules.emplace_back(".*", "regular_signal");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "regular_signal");
    }
}

TEST_CASE("TemplateMatcher - warns on constant assigned to output", "[template]") {
    DiagnosticCollector diag;
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.rules.emplace_back(".*", "'0");

    TemplateMatcher matcher(&tmpl, &diag);
    matcher.setInstance("u_sub");

    SECTION("no warning for input port") {
        PortInfo port("data_in", "input");
        (void)matcher.matchPort(port);
        CHECK(diag.warningCount() == 0);
    }

    SECTION("warning for output port") {
        PortInfo port("data_out", "output");
        (void)matcher.matchPort(port);
        CHECK(diag.warningCount() == 1);
        CHECK(diag.format().find("Constant ''0' assigned to output port") != std::string::npos);
    }

    SECTION("no warning for inout port") {
        PortInfo port("data_io", "inout");
        (void)matcher.matchPort(port);
        CHECK(diag.warningCount() == 0);
    }
}

TEST_CASE("TemplateMatcher - first matching rule wins", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.rules.emplace_back("data_.*", "first_match");
    tmpl.rules.emplace_back("data_in", "second_match");

    TemplateMatcher matcher(&tmpl);
    matcher.setInstance("u_sub");

    PortInfo port("data_in", "input");
    auto result = matcher.matchPort(port);

    CHECK(result.signal_name == "first_match");
}

TEST_CASE("TemplateMatcher - @ alias for %1", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.instance_pattern = "u_sub_(\\d+)";
    tmpl.rules.emplace_back("data", "data_@");

    TemplateMatcher matcher(&tmpl);
    matcher.setInstance("u_sub_5");

    PortInfo port("data", "input", 8);
    auto result = matcher.matchPort(port);

    CHECK(result.signal_name == "data_5");
}

TEST_CASE("TemplateMatcher - default pattern extracts first number", "[template]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.instance_pattern = "";  // Empty = use default pattern
    tmpl.rules.emplace_back("data", "data_@");

    TemplateMatcher matcher(&tmpl);

    SECTION("Number at end") {
        matcher.setInstance("u_sub_42");
        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "data_42");
    }

    SECTION("Number in middle") {
        matcher.setInstance("ms2m");
        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "data_2");
    }

    SECTION("Multiple numbers - first one wins") {
        matcher.setInstance("u_inst123_abc456");
        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "data_123");
    }
}

TEST_CASE("TemplateMatcher - math functions", "[template][math]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    tmpl.instance_pattern = "";  // Use default pattern

    SECTION("add function") {
        tmpl.rules.emplace_back("data", "net_add(@, 1)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_0");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_1");  // 0 + 1 = 1
    }

    SECTION("sub function") {
        tmpl.rules.emplace_back("data", "net_sub(@, 1)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_5");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_4");  // 5 - 1 = 4
    }

    SECTION("mul function") {
        tmpl.rules.emplace_back("data", "net_mul(@, 2)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_3");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_6");  // 3 * 2 = 6
    }

    SECTION("div function") {
        tmpl.rules.emplace_back("data", "net_div(@, 2)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_7");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_3");  // 7 / 2 = 3 (integer division)
    }

    SECTION("mod function") {
        tmpl.rules.emplace_back("data", "net_mod(@, 2)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_5");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_1");  // 5 % 2 = 1
    }

    SECTION("nested functions") {
        // (@ + 1) % 2: u_sub_0 -> (0+1)%2 = 1, u_sub_1 -> (1+1)%2 = 0
        tmpl.rules.emplace_back("data", "net_mod(add(@, 1), 2)");
        TemplateMatcher matcher(&tmpl);

        matcher.setInstance("u_sub_0");
        PortInfo port("data", "input");
        auto result0 = matcher.matchPort(port);
        CHECK(result0.signal_name == "net_1");  // (0+1)%2 = 1

        matcher.setInstance("u_sub_1");
        auto result1 = matcher.matchPort(port);
        CHECK(result1.signal_name == "net_0");  // (1+1)%2 = 0
    }

    SECTION("deeply nested functions") {
        // add(mul(2, 3), 1) = 2*3+1 = 7
        tmpl.rules.emplace_back("data", "net_add(mul(2, 3), 1)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_0");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_7");
    }

    SECTION("multiple functions in expression") {
        tmpl.rules.emplace_back("data", "a_add(@, 1)_b_mul(@, 2)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_3");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "a_4_b_6");  // add(3,1)=4, mul(3,2)=6
    }
}

TEST_CASE("TemplateMatcher - math function edge cases", "[template][math]") {
    AutoTemplate tmpl;
    tmpl.module_name = "submod";
    DiagnosticCollector diag;

    SECTION("division by zero") {
        tmpl.rules.emplace_back("data", "net_div(@, 0)");
        TemplateMatcher matcher(&tmpl, &diag);
        matcher.setInstance("u_sub_5");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_0");  // Returns 0 on div by zero
        CHECK(diag.warningCount() == 1);
    }

    SECTION("modulo by zero") {
        tmpl.rules.emplace_back("data", "net_mod(@, 0)");
        TemplateMatcher matcher(&tmpl, &diag);
        matcher.setInstance("u_sub_5");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_0");  // Returns 0 on mod by zero
        CHECK(diag.warningCount() == 1);
    }

    SECTION("negative result") {
        tmpl.rules.emplace_back("data", "net_sub(@, 10)");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_3");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "net_-7");  // 3 - 10 = -7
    }

    SECTION("no functions - unchanged") {
        tmpl.rules.emplace_back("data", "regular_signal");
        TemplateMatcher matcher(&tmpl);
        matcher.setInstance("u_sub_0");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);
        CHECK(result.signal_name == "regular_signal");
    }
}

TEST_CASE("TemplateMatcher - invalid regex warnings", "[template][errors]") {
    DiagnosticCollector diag;

    SECTION("invalid port pattern emits warning") {
        AutoTemplate tmpl;
        tmpl.module_name = "submod";
        // Invalid regex: unmatched bracket
        tmpl.rules.emplace_back("[invalid", "signal_$1");

        TemplateMatcher matcher(&tmpl, &diag);
        matcher.setInstance("u_sub");

        PortInfo port("data", "input");
        auto result = matcher.matchPort(port);

        // Should fall through to default (port name)
        CHECK(result.signal_name == "data");
        // Should have emitted a warning about invalid regex
        CHECK(diag.warningCount() == 1);
    }

    SECTION("invalid instance pattern emits warning") {
        AutoTemplate tmpl;
        tmpl.module_name = "submod";
        tmpl.instance_pattern = "[invalid";  // Invalid regex
        tmpl.rules.emplace_back("data", "signal_$1");

        TemplateMatcher matcher(&tmpl, &diag);
        // setInstance should handle invalid regex gracefully
        bool result = matcher.setInstance("u_sub_0");
        // Returns true if instance matches as literal (it won't)
        CHECK_FALSE(result);
        CHECK(diag.warningCount() == 1);
    }
}
