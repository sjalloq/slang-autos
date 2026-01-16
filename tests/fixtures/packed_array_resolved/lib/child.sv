// Child module with packed array ports
module child (
    input  logic clk,
    input  logic [3:0][7:0] packed_data_in,
    output logic [3:0][7:0] packed_data_out
);
endmodule
