// Test multiple AUTOINST in one module
module multiple_autoinst (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out,
    output logic        valid
);

    // First instance
    submod u_first (/*AUTOINST*/);

    // Second instance
    submod u_second (/*AUTOINST*/);

endmodule
