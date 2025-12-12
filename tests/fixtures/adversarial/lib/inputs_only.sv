// Module with only input ports (sink)
module inputs_only (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in
);
    // Just a sink - does nothing visible
endmodule
