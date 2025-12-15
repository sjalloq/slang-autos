// Test: single-bit slice uses simple [0] format
// one_bit (1-bit) and wide (16-bit) connect to same signals
// Expected: one_bit gets [0] slice
module top_single_bit;
    /*AUTOLOGIC*/

    wide u_wide (/*AUTOINST*/);
    one_bit u_one_bit (/*AUTOINST*/);
endmodule
