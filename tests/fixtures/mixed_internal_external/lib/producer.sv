// Producer module - outputs data that will be consumed by consumer
module producer (
    input  logic        clk,
    input  logic        rst_n,
    output logic [7:0]  data_out,    // -> goes to consumer (INTERNAL)
    output logic        data_valid   // -> goes to consumer (INTERNAL)
);
endmodule
