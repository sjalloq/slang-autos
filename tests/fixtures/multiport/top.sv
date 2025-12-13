// Test case: AUTOINST with module that has shared port declarations
module top(
    input clk,
    input rst_n,
    input [7:0] a, b, c,
    output [7:0] out
);
    submod u_sub (/*AUTOINST*/);
endmodule
