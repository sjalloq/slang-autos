// Child module with multiple outputs that get concatenated
module producer (
    input  logic        clk,
    input  logic [7:0]  data_in,
    output logic [3:0]  sig_a,
    output logic [3:0]  sig_b
);
endmodule
