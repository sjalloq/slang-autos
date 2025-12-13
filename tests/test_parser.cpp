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
            /* submod AUTO_TEMPLATE (
               data_in => my_data_in
               data_out => my_data_out
            ); */
        )");

        REQUIRE(parser.templates().size() == 1);
        auto& tmpl = parser.templates()[0];
        CHECK(tmpl.module_name == "submod");
        CHECK(tmpl.instance_pattern.empty());  // No instance pattern
        REQUIRE(tmpl.rules.size() == 2);
        CHECK(tmpl.rules[0].port_pattern == "data_in");
        CHECK(tmpl.rules[0].signal_expr == "my_data_in");
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

