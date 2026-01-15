// Test case: Concatenation in template should extract signal name for AUTOPORTS
// Template maps sig_a => {1'b0, sig_a}
// AUTOPORTS should generate: input logic [2:0] sig_a (extracted signal name)
// NOT: input logic [2:0] {1'b0, sig_a} (invalid - concatenation as port name)
module top(
/*AUTOPORTS*/
);

    /* child AUTO_TEMPLATE
       sig_a => {1'b0, sig_a}
     */

    child u_child (/*AUTOINST*/);
endmodule
