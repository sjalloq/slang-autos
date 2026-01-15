// Child module with a generic input port
module child (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [2:0]  sig_a,
    output logic [7:0]  data_out
);
endmodule
