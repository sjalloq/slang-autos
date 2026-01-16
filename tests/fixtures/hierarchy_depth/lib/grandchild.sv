// Grandchild module with include that doesn't exist
// This tests that errors in grandchildren don't block top-level expansion
`include "nonexistent.vh"

module grandchild (
    input  logic clk,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    assign data_out = data_in;
endmodule
