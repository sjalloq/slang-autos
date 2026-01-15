// Test closer to user's example
module top2 (
    input logic clk
    /*AUTOPORTS*/
);

    /* child2 AUTO_TEMPLATE
        amf_msi_rc_int([0-9])_csr_fifo_ovrflw_in => amf_rc_int_ovrflw[$1],
     */
    child2 u_child (/*AUTOINST*/);

endmodule
