// Test: output declared AFTER AUTOPORTS marker
module top4 #(
    parameter NumRcInt = 8
)(
    input  logic clk
    /*AUTOPORTS*/,
    output logic [NumRcInt-1:0] [2:0] amf_rc_int_fifo_level
);

    /* child AUTO_TEMPLATE
        data_in([0-9]) => amf_rc_int_fifo_level[$1],
     */
    child u_child (/*AUTOINST*/);

endmodule
