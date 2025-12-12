// Module with very wide buses
module wide_bus (
    input  logic          clk,
    input  logic          rst_n,
    input  logic [255:0]  wide_data_in,
    input  logic [1023:0] very_wide_data_in,
    output logic [255:0]  wide_data_out,
    output logic [1023:0] very_wide_data_out
);
    assign wide_data_out = wide_data_in;
    assign very_wide_data_out = very_wide_data_in;
endmodule
