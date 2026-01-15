// Test case: Signals driven by assign LHS concatenation
// WORKAROUND: Declare sig_a and sig_b locally to prevent them appearing in AUTOPORTS
// The assign statement splits pipelined_signals into sig_a and sig_b
//
// KNOWN LIMITATION: slang-autos doesn't track assign statements for signal direction,
// so pipelined_signals won't automatically appear as an input port.
module top (
    input logic clk,
    input logic [7:0] pipelined_signals  // Must be declared manually
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    // Split incoming signal into parts via concatenation
    // These MUST be declared locally to prevent appearing in AUTOPORTS
    logic [3:0] sig_a;
    logic [3:0] sig_b;
    assign {sig_a, sig_b} = pipelined_signals;

    // Manual instance using the split signals
    child u_child (
        .clk      (clk),
        .sig_a    (sig_a),
        .sig_b    (sig_b),
        .data_out (data_out)
    );

endmodule
