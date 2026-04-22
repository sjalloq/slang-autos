module top (
    input        clk,
    input        rst_n,
    input  [7:0] data_in,
    output [7:0] data_out,
    output       valid
);

    submod u_submod (
        .*
    );

endmodule
