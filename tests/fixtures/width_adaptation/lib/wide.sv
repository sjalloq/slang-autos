// 16-bit port module for width adaptation tests
module wide(
    input  logic        clk,
    input  logic [15:0] data_in,   // wider input
    output logic [15:0] data_out   // wider output
);
endmodule
