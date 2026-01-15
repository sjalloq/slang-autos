// Top module without struct typedef - relies on child's definition
// Testing if struct members get expanded incorrectly
module top2 (
    input  logic clk
    /*AUTOPORTS*/
);
    child u_child (/*AUTOINST*/);
endmodule
