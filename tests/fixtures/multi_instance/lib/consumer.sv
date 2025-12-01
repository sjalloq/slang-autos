module consumer(
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data,      // 8-bit input (narrower than producer - tests width conflict)
    input  logic        valid,
    output logic [31:0] result,
    output logic        result_valid
);
    // Consumer processes data and produces result
endmodule
