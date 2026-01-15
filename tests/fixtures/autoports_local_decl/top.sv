// Test case: Local declarations should NOT be added to AUTOPORTS
// The signal local_sig is declared locally as 'logic' and connected via template.
// It should NOT appear in AUTOPORTS because it's not an external signal.
module top(
/*AUTOPORTS*/
);
    // Local declaration - this should NOT be added to AUTOPORTS
    logic [2:0] local_sig;

    assign local_sig = 3'b101;  // Some local logic

    /* child AUTO_TEMPLATE
       sig_a => local_sig
     */

    child u_child (/*AUTOINST*/);
endmodule
