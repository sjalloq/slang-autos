// Test: slice-down when port < signal width
// narrow (8-bit) and wide (16-bit) connect to same signals
// Expected: narrow gets sliced [7:0], wide gets full width
module top_slice;
    /*AUTOLOGIC*/

    wide u_wide (/*AUTOINST*/);
    narrow u_narrow (/*AUTOINST*/);
endmodule
