// Test with output signals
module top3 (
    input logic clk
    /*AUTOPORTS*/
);

    /* child3 AUTO_TEMPLATE
        data_out([0-9]) => data_out[$1],
     */
    child3 u_child (/*AUTOINST*/);

endmodule
