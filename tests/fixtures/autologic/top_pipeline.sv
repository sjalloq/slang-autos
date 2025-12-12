// Test fixture for AUTOLOGIC - SystemVerilog internal logic declarations
// This module has:
// - External inputs: clk, rst_n (consumed by both instances but not driven)
// - Internal logic: data_out, data_valid (driven by producer output, consumed by consumer input)
// - External outputs: result, result_valid (driven by consumer output, not consumed)
//
// Expected after expansion:
// - AUTOPORTS: input clk, input rst_n, output [15:0] result, output result_valid
// - AUTOLOGIC: logic [7:0] data_out, logic data_valid
module top_pipeline (
    /*AUTOPORTS*/
);
    /*AUTOLOGIC*/

    /* producer AUTO_TEMPLATE (
        data_out   => inter_data,
        data_valid => inter_valid,
    ); */
    producer u_prod (/*AUTOINST*/);

    /* consumer AUTO_TEMPLATE (
        data_in     => inter_data,
        data_ready  => inter_valid,
    ); */
    consumer u_cons (/*AUTOINST*/);

endmodule
