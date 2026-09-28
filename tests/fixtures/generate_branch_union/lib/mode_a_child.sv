module mode_a_child #(
    parameter int W = 8
) (
    input  logic         clk,
    input  logic [W-1:0] a_in,
    output logic [W-1:0] a_out
);
endmodule
