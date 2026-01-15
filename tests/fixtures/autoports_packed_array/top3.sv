// Reproduce user bug: output used by child inputs should not be duplicated
module top3 #(
    parameter NumRcInt = 8
)(
    input  logic clk,
    output logic [NumRcInt-1:0] [2:0] amf_rc_int_fifo_level
    /*AUTOPORTS*/
);

    /* child AUTO_TEMPLATE
        data_in([0-9]) => amf_rc_int_fifo_level[$1],
     */
    child u_child (/*AUTOINST*/);

endmodule
