// Simple submodule for adversarial tests
module submod (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out,
    output logic        valid
);
    assign data_out = data_in;
    assign valid = 1'b1;
endmodule
