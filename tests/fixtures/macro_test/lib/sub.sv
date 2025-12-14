`define DATA_WIDTH 8

module sub (
    input  logic              clk,
    input  logic [`DATA_WIDTH-1:0] data_in,
    output logic [`DATA_WIDTH-1:0] data_out
);
endmodule
