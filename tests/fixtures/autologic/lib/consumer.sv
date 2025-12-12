// Consumer module for AUTOLOGIC testing
module consumer(
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    input  logic        data_ready,
    output logic [15:0] result,
    output logic        result_valid
);
    assign result = {8'h00, data_in};
    assign result_valid = data_ready;
endmodule
