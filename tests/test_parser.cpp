#include <catch2/catch_test_macros.hpp>

#include "slang-autos/Parser.h"
#include "slang-autos/Diagnostics.h"

using namespace slang_autos;

TEST_CASE("AutoParser - parse AUTO_TEMPLATE", "[parser]") {
    DiagnosticCollector diag;
    AutoParser parser(&diag);

    SECTION("Simple template with rules") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE "u_.*"
               data_in => my_data_in
               data_out => my_data_out
            */
        )");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        CHECK(tmpl.module_name == "submod");
        CHECK(tmpl.instance_pattern == "u_.*");
        REQUIRE(tmpl.rules.size() == 2);
        CHECK(tmpl.rules[0].port_pattern == "data_in");
        CHECK(tmpl.rules[0].signal_expr == "my_data_in");
    }

    SECTION("Template with capture groups") {
        parser.parseText(R"sv(
            /* fifo AUTO_TEMPLATE "u_fifo_(\d+)"
               din => fifo_%1_din
               dout => fifo_%1_dout
            */
        )sv");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        CHECK(tmpl.module_name == "fifo");
        CHECK(tmpl.instance_pattern == "u_fifo_(\\d+)");
    }

    SECTION("Template with no rules warns") {
        parser.parseText(R"(
            /* empty_mod AUTO_TEMPLATE "u_.*"
            */
        )");

        // Should still parse but generate warning
        CHECK(parser.templates().size() == 1);
        CHECK(diag.warningCount() > 0);
    }

    SECTION("Template without instance pattern") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE
               data_in => my_data_in
               data_out => my_data_out
            */
        )");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        CHECK(tmpl.module_name == "submod");
        CHECK(tmpl.instance_pattern.empty());  // No instance pattern
        REQUIRE(tmpl.rules.size() == 2);
        CHECK(tmpl.rules[0].port_pattern == "data_in");
        CHECK(tmpl.rules[0].signal_expr == "my_data_in");
    }

    SECTION("Verilog-mode wrapper parens are rejected") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE (
               data_in => my_data_in
               data_out => my_data_out
            ); */
        )");

        CHECK(parser.templates().empty());
        CHECK(diag.errorCount() > 0);

        bool found = false;
        for (const auto& d : diag.diagnostics()) {
            if (d.type == "template_syntax" &&
                d.message.find("Unexpected content") != std::string::npos) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    SECTION("Invalid instance pattern regex rejects template") {
        parser.parseText(R"sv(
            /* submod AUTO_TEMPLATE "u_["
               data_in => sig_a
            */
        )sv");

        CHECK(parser.templates().empty());
        CHECK(diag.errorCount() > 0);

        bool found = false;
        for (const auto& d : diag.diagnostics()) {
            if (d.type == "template_regex" &&
                d.message.find("instance pattern") != std::string::npos) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    SECTION("Balanced parentheses in signal expression are preserved") {
        parser.parseText(R"sv(
            /* submod AUTO_TEMPLATE
               data_([0-9]) => net_add($1, 1)
               mode => (a | b)
            */
        )sv");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        REQUIRE(tmpl.rules.size() == 2);
        CHECK(tmpl.rules[0].signal_expr == "net_add($1, 1)");
        CHECK(tmpl.rules[1].signal_expr == "(a | b)");
        CHECK(diag.errorCount() == 0);
    }

    SECTION("Unbalanced trailing paren rejects whole template") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE
               data_in  => sig_a
               data_out => sig_b
               enable   => sig_c)
            */
        )");

        CHECK(parser.templates().empty());
        CHECK(diag.errorCount() > 0);

        bool found = false;
        for (const auto& d : diag.diagnostics()) {
            if (d.type == "template_syntax" &&
                d.message.find("Unbalanced parentheses") != std::string::npos) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    SECTION("Unbalanced leading paren rejects whole template") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE
               data_in => (sig_a
            */
        )");

        CHECK(parser.templates().empty());
        CHECK(diag.errorCount() > 0);
    }
}

TEST_CASE("AutoParser - parse AUTOINST", "[parser]") {
    DiagnosticCollector diag;
    AutoParser parser(&diag);

    SECTION("Simple AUTOINST") {
        parser.parseText(R"(
            module_type inst_name (
                .clk(clk),
                /*AUTOINST*/
            );
        )");

        REQUIRE(parser.autoinsts().size() == 1);
        CHECK(!parser.autoinsts()[0].filter_pattern.has_value());
    }

    SECTION("AUTOINST with filter") {
        parser.parseText(R"(
            module_type inst_name (
                /*AUTOINST("data_.*")*/
            );
        )");

        REQUIRE(parser.autoinsts().size() == 1);
        REQUIRE(parser.autoinsts()[0].filter_pattern.has_value());
        CHECK(*parser.autoinsts()[0].filter_pattern == "data_.*");
    }
}

TEST_CASE("AutoParser - parse AUTOLOGIC", "[parser]") {
    DiagnosticCollector diag;
    AutoParser parser(&diag);

    SECTION("Simple AUTOLOGIC") {
        parser.parseText(R"(
            module top;
                /*AUTOLOGIC*/

                submod u_sub (/*AUTOINST*/);
            endmodule
        )");

        CHECK(parser.autologics().size() == 1);
    }
}

TEST_CASE("AutoParser - template comments", "[parser]") {
    DiagnosticCollector diag;
    AutoParser parser(&diag);

    SECTION("Line comment with //") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE
               data_in => my_data_in   // Input signal
               data_out => my_data_out // Output signal
            */
        )");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        REQUIRE(tmpl.rules.size() == 2);
        CHECK(tmpl.rules[0].port_pattern == "data_in");
        CHECK(tmpl.rules[0].signal_expr == "my_data_in");
        CHECK(tmpl.rules[1].port_pattern == "data_out");
        CHECK(tmpl.rules[1].signal_expr == "my_data_out");
    }

    // Note: /* */ inline comments inside templates are NOT supported because
    // Verilog block comments don't nest. The first */ would close the template.
    // Use // line comments instead.

    SECTION("Comment-only lines are ignored") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE
               // Clock and reset
               clk => sys_clk
               rst_n => sys_rst_n
               // Data signals
               data => bus_data
            */
        )");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        REQUIRE(tmpl.rules.size() == 3);
        CHECK(tmpl.rules[0].signal_expr == "sys_clk");
        CHECK(tmpl.rules[1].signal_expr == "sys_rst_n");
        CHECK(tmpl.rules[2].signal_expr == "bus_data");
    }

    SECTION("Multiple line comments") {
        parser.parseText(R"(
            /* submod AUTO_TEMPLATE
               // Group 1
               port_a => sig_a    // inline comment
               port_b => sig_b    // another comment
               // Group 2
               port_c => sig_c
            */
        )");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        REQUIRE(tmpl.rules.size() == 3);
        CHECK(tmpl.rules[0].signal_expr == "sig_a");
        CHECK(tmpl.rules[1].signal_expr == "sig_b");
        CHECK(tmpl.rules[2].signal_expr == "sig_c");
    }
}

