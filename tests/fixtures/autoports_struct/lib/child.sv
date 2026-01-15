// Child module with struct-typed port
typedef struct packed {
    logic [7:0] field_a;
    logic [3:0] field_b;
} my_struct_t;

module child (
    input  logic      clk,
    input  my_struct_t data_in,
    output my_struct_t data_out
);
endmodule
