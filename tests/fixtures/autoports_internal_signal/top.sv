// Test that signals in output concatenation don't become ports incorrectly
// Scenario: Template maps an output port to a concatenation
// {sig_a, sig_b} should NOT become output ports - they're internal wires
module top (
    input logic clk
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    /* child AUTO_TEMPLATE
        data_in  => data_in,
        data_out => {sig_a, sig_b},
     */
    child u_child (/*AUTOINST*/);

endmodule
