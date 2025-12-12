// Test that AUTOINST inside strings is NOT expanded
module autoinst_in_string (
    input  logic clk,
    input  logic rst_n
);

    // This string contains AUTOINST but should NOT be expanded
    localparam string HELP_TEXT = "Use /*AUTOINST*/ to auto-connect ports";

    // Real AUTOINST that SHOULD be expanded
    submod u_sub (/*AUTOINST*/);

endmodule
