// Test: Signals consumed by inline wire declarations should not become output ports
module top (
    input logic clk
    /*AUTOPORTS*/
);
    /*AUTOLOGIC*/

    // sig_a is consumed by the inline wire declaration
    // It should NOT become an output port
    wire unused_ok = &{1'b0, sig_a};

    child u_child (/*AUTOINST*/);
endmodule
