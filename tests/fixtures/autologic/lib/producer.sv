// Producer module for AUTOLOGIC testing
module producer(
    input  logic        clk,
    input  logic        rst_n,
    output logic [7:0]  data_out,
    output logic        data_valid
);
    assign data_out = 8'hAA;
    assign data_valid = 1'b1;
endmodule
