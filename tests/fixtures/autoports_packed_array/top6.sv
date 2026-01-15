// Test: signal used by both producer and consumer instances
module top6 #(
    parameter NumRcInt = 8
)(
    input  logic clk,
    output logic [NumRcInt-1:0] [2:0] amf_rc_int_fifo_level
    /*AUTOPORTS*/
);

    // Producer writes to the signal
    /* producer AUTO_TEMPLATE
        fifo_level => amf_rc_int_fifo_level[0],
     */
    producer u_producer (/*AUTOINST*/);

    // Consumer reads from the signal
    /* child AUTO_TEMPLATE
        data_in([0-9]) => amf_rc_int_fifo_level[$1],
     */
    child u_child (/*AUTOINST*/);

endmodule
