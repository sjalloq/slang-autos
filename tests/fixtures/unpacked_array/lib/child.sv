// Child module with unpacked array ports
module child (
    input  logic clk,
    input  logic [7:0] unpacked_data_in [3:0],
    output logic [7:0] unpacked_data_out [3:0]
);
endmodule
