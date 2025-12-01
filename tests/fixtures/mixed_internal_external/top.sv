// Test fixture for mixed internal/external signals
// This module has:
// - External inputs: clk, rst_n (consumed by both instances but not driven)
// - Internal wires: data_out, data_valid (driven by producer output, consumed by consumer input)
// - External outputs: result, result_valid (driven by consumer output, not consumed)
//
// Expected after expansion:
// - AUTOPORTS: input clk, input rst_n, output [15:0] result, output result_valid
// - AUTOWIRE: logic [7:0] data_out, logic data_valid
module top (
    /*AUTOPORTS*/
);
    /*AUTOWIRE*/

    producer u_prod (/*AUTOINST*/);

    consumer u_cons (/*AUTOINST*/);

endmodule
