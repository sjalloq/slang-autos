module submod #(
    parameter int WIDTH = 3
)(
    input logic clk,
    input logic rst_n,
    input logic [WIDTH-1:0] a,
    output logic b,
    output logic [WIDTH-1:0] c
);
endmodule