module producer(
    input  logic        clk,
    input  logic        rst_n,
    output logic [15:0] data,      // 16-bit output (for width conflict test)
    output logic        valid
);
    // Producer generates data
endmodule
