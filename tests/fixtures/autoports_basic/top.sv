module top (
    input  logic clk,
    input  logic rst_n,
    /*AUTOPORTS*/
    output logic valid,
    output logic [7:0] data_out,
    input logic [7:0] data_in
);
    /*AUTOWIRE*/

    submod u_sub (/*AUTOINST*/
        // Outputs
        .data_out(data_out),
        .valid(valid),
        // Inputs
        .clk(clk),
        .rst_n(rst_n),
        .data_in(data_in)
    );
endmodule
