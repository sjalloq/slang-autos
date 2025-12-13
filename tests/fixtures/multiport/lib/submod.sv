// Test case: Module with shared port declarations (MultiPortSymbol)
module submod(
    input clk, rst_n,           // Two inputs sharing declaration
    input [7:0] a, b, c,        // Three inputs sharing declaration with width
    output [7:0] out
);
endmodule
