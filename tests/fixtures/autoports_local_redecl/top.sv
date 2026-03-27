module top (
    input  logic clk,
    input  logic rst_n
    /*AUTOPORTS*/,
    output logic  [7:0] data_out,
    output logic valid,
    input logic  [7:0] data_in
);
    // User added this local declaration after initial expansion.
    // On re-expansion, data_in should be removed from AUTOPORTS.
    logic [7:0] data_in;

    submod u_sub (/*AUTOINST*/
        // Outputs
        .data_out (data_out),
        .valid    (valid),
        // Inputs
        .clk      (clk),
        .rst_n    (rst_n),
        .data_in  (data_in)
    );
endmodule
