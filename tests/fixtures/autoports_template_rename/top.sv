// Bug test: Template with capture groups should add renamed signal to AUTOPORTS
module top (
    input logic clk
    /*AUTOPORTS*/
);

    /* child AUTO_TEMPLATE
        data_in([0-9]) => data_in[$1],
        data_out([0-9]) => data_out[$1],
     */
    child u_child (/*AUTOINST*/);

endmodule
