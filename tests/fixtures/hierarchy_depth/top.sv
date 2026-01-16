// Top module - should expand despite grandchild having missing include
// slang-autos-libdir: lib

module top (
    input logic clk
    /*AUTOPORTS*/
);
    /*AUTOLOGIC*/
    child u_child (/*AUTOINST*/);
endmodule
