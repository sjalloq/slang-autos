typedef struct packed {
    logic a;
    logic b;
} my_t;

module top #(
    parameter integer Width = 32
)(
    input logic clk,
    input logic reset_n,
    input my_t my_type
    /*AUTOPORTS*/
);

    /* child AUTO_TEMPLATE
        a => my_type.a,
        b => my_type.b,
     */
    child u_child (/*AUTOINST*/);

endmodule
