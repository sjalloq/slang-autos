// Test case: `ifdef/`endif block between manual ports and /*AUTOINST*/.
// The backwards comma scan must skip preprocessor directive lines so it
// does not insert a spurious comma right after the AUTOINST marker.
`define USE_OPT_PORT

module top(
    input  logic       clk,
    input  logic       rst_n,
    input  logic       opt_in,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    submod u_sub (
        .clk(clk),
`ifdef USE_OPT_PORT
        .opt_in(opt_in),
`endif
        /*AUTOINST*/
    );
endmodule
