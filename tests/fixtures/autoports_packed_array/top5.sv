// Test: output followed by another port before AUTOPORTS
module top5 #(
    parameter NumRcInt = 8
)(
    input  logic clk,
    output logic [NumRcInt-1:0] [2:0] amf_rc_int_fifo_level,
    input  logic reset_n
    /*AUTOPORTS*/
);

    /* child AUTO_TEMPLATE
        data_in([0-9]) => amf_rc_int_fifo_level[$1],
     */
    child u_child (/*AUTOINST*/);

endmodule
