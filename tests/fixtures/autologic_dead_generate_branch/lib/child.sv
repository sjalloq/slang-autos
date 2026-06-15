module child (
    input  logic       clk,
    output logic [7:0] data_out
);
    assign data_out = 8'h42;
endmodule
