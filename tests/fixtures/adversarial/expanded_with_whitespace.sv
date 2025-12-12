// Test idempotency with user-inserted whitespace
module expanded_with_whitespace (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out,
    output logic        valid
);

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
