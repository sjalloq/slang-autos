// 8-bit port module for width adaptation tests
module narrow(
    input  logic        clk,
    input  logic [7:0]  data_in,   // narrower input
    output logic [7:0]  data_out   // narrower output
);
endmodule
