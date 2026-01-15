// Bug: Manually declared output should not be duplicated as input in AUTOPORTS
module top #(
    parameter NumItems = 4
)(
    input  logic clk,
    output logic [NumItems-1:0][2:0] data_out
    /*AUTOPORTS*/
);

    /* child AUTO_TEMPLATE
        data_in([0-9]) => data_out[$1],
     */
    child u_child (/*AUTOINST*/);

endmodule
