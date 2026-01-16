// Child module that instantiates grandchild
module child (
    input  logic clk,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    grandchild u_grandchild (
        .clk      (clk),
        .data_in  (data_in),
        .data_out (data_out)
    );
endmodule
