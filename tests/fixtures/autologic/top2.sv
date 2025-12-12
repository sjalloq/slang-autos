// Top module using AUTOLOGIC (SystemVerilog style)
// This test has internal nets between two instances
module top2(
    input  logic       clk,
    input  logic       rst_n
);

    /*AUTOLOGIC*/

    // First instance produces intermediate signal
    sub u_sub1 (
        .clk      (clk),
        .rst_n    (rst_n),
        .data_in  (8'h00),
        .data_out (intermediate)  // This net needs declaration
    );

    // Second instance consumes intermediate signal
    sub u_sub2 (
        .clk      (clk),
        .rst_n    (rst_n),
        .data_in  (intermediate),  // Same net - internal connection
        .data_out ()               // Output unused
    );

endmodule
