// Sink module:
//   ctrl   - driven by the parent's always_comb (internal -> must NOT be an input port)
//   result - genuine external output
module sink (
    input  logic       clk,
    input  logic [3:0] ctrl,
    output logic [3:0] result
);
endmodule
