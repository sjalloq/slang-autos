// Test case: port widths written with macros, including a macro that expands
// to several tokens via a nested macro. With resolved-ranges off, the original
// macro text must be copied exactly once, not once per expanded token.
`include "defs.svh"
module top (
    input logic clk,
    /*AUTOPORTS*/
);
    /*AUTOLOGIC*/
    child u_child (
        .clk (clk),
        /*AUTOINST*/
    );
endmodule
