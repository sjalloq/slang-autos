// Test case: trailing comments after the last manual port connection, before
// /*AUTOINST*/. The backwards comma scan must skip comments so it does not
// insert a spurious comma right after the AUTOINST marker.
module top(
    input  logic       clk,
    input  logic       rst_n,
    input  logic       sel_bit,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    submod u_sub (
        .clk                         (clk),                       // input
        .sel                         ({3{sel_bit}}),              /* input */
        /*AUTOINST*/
    );
endmodule
