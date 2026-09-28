// Test case: each generate branch instantiates a module that exists nowhere
// else in the design. Expansion must cover both branches in a single pass,
// whatever value MODE takes, and the result must not depend on -G overrides.
// The instance in g_mode_a overrides W, so its ports must resolve to that
// width rather than copying the child's parameter name into this scope.
module top #(
    parameter bit MODE = 0
) (
    input logic clk,
    /*AUTOPORTS*/
);
    /*AUTOLOGIC*/

    if (MODE) begin : g_mode_a
        mode_a_child #(.W(16)) u_a (
            .clk (clk),
            /*AUTOINST*/
        );
    end else begin : g_mode_b
        mode_b_child u_b (
            .clk (clk),
            /*AUTOINST*/
        );
    end
endmodule
