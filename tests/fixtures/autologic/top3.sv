// Top module using AUTOLOGIC with AUTOINST
// Tests internal logic declarations for SystemVerilog
module top3(
    input  logic       clk,
    input  logic       rst_n
);

    /*AUTOLOGIC*/

    /* sub AUTO_TEMPLATE (
        .data_in   (stage1_data),
        .data_out  (stage2_data),
    ); */
    sub u_sub1 (/*AUTOINST*/);

    /* sub AUTO_TEMPLATE (
        .data_in   (stage2_data),
        .data_out  (stage3_data),
    ); */
    sub u_sub2 (/*AUTOINST*/);

endmodule
