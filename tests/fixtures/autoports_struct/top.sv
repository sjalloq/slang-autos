// Top module manually declares struct port
// Bug: slang-autos should NOT add struct members as separate ports
typedef struct packed {
    logic [7:0] field_a;
    logic [3:0] field_b;
} my_struct_t;

module top (
    input  logic clk,
    input  my_struct_t data_in,
    output my_struct_t data_out
    /*AUTOPORTS*/
);
    child u_child (/*AUTOINST*/);
endmodule
