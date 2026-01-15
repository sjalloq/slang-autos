// Test case: Signals on RHS of assign should NOT become output ports
// The assign statement consumes sig_a and sig_b internally
// So they should NOT appear in AUTOPORTS as outputs
module top_rhs (
    input logic clk
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    // sig_a and sig_b are outputs from producer instance
    // They are consumed by the assign statement (RHS)
    // They should NOT become output ports - they're consumed internally
    assign sig_bus = {sig_a, sig_b};

    producer u_producer (
        .clk      (clk),
        .data_in  (data_in),
        .sig_a    (sig_a),
        .sig_b    (sig_b)
    );

endmodule
