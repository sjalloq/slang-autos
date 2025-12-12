// Test wide bus handling
module wide_bus_top (
    input  logic          clk,
    input  logic          rst_n,
    input  logic [255:0]  wide_data_in,
    input  logic [1023:0] very_wide_data_in,
    output logic [255:0]  wide_data_out,
    output logic [1023:0] very_wide_data_out
);

    wide_bus u_wide (/*AUTOINST*/);

endmodule
