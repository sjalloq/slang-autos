// Test case: Manual port without trailing comma before AUTOINST
module top_no_comma(
    input logic clk,
    input logic rst_n,
    input logic [7:0] data_in,
    output logic [7:0] data_out
);
    submod u_sub (
        .clk(clk)
        /*AUTOINST*/
    );
endmodule
