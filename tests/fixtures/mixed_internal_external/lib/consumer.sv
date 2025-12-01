// Consumer module - receives data from producer, outputs result externally
// Port names match producer's outputs so AUTOINST can connect them automatically
module consumer (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_out,    // <- comes from producer (INTERNAL) - matches producer's output name
    input  logic        data_valid,  // <- comes from producer (INTERNAL) - matches producer's output name
    output logic [15:0] result,      // -> goes outside (EXTERNAL OUTPUT)
    output logic        result_valid // -> goes outside (EXTERNAL OUTPUT)
);
endmodule
