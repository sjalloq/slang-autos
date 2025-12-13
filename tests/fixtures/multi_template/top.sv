// Test case: Multiple templates for the same module
// Each instance should use the closest preceding template (verilog-mode semantics)
module top(
    input logic clk,
    input logic [7:0] sig_a_data_in,
    input logic [7:0] sig_b_data_in,
    output logic [7:0] sig_a_data_out,
    output logic [7:0] sig_b_data_out
);

    // Template for first instance - prefixes signals with sig_a_
    /* submod AUTO_TEMPLATE
       .* => sig_a_$0,
    */
    submod u_sub_a (/*AUTOINST*/);

    // Template for second instance - prefixes signals with sig_b_
    /* submod AUTO_TEMPLATE
       .* => sig_b_$0,
    */
    submod u_sub_b (/*AUTOINST*/);

endmodule
