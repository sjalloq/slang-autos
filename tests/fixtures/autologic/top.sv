// Top module using AUTOLOGIC (SystemVerilog style)
module top(
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] in_data,
    output logic [7:0] out_data
);

    /*AUTOLOGIC*/

    sub u_sub1 (/*AUTOINST*/);

    sub u_sub2 (
        .clk        (clk),
        .rst_n      (rst_n),
        .data_in    (intermediate),
        .data_out   (out_data)
    );

    // First sub processes input, second sub outputs
    assign intermediate = data_out;  // From u_sub1

endmodule
