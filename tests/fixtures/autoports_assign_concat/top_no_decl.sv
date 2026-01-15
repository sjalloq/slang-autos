// Test case: Signals on LHS of assign should NOT become input ports
// The assign statement drives sig_a and sig_b internally
// So they should NOT appear in AUTOPORTS as inputs
module top_no_decl (
    input logic clk
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    // sig_a and sig_b are driven by the assign statement (LHS)
    // They should NOT become input ports
    assign {sig_a, sig_b} = pipelined_signals;

    // Manual instance using the split signals
    child u_child (
        .clk      (clk),
        .sig_a    (sig_a),
        .sig_b    (sig_b),
        .data_out (data_out)
    );

endmodule
