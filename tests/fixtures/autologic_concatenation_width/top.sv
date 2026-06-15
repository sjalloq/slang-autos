// Regression: a signal used inside a concatenation must take its own width,
// not the width of the port the concatenation drives/feeds. Here sig_a/b/c
// are 1-bit producer outputs concatenated into sink's 3-bit `bus` input; the
// AUTOLOGIC declarations must be 1-bit, not [2:0].
module top (
    input logic clk
);

    /*AUTOLOGIC*/

    producer u_p (
        /*AUTOINST*/
    );

    sink u_s (
        .bus({sig_a, sig_b, sig_c})
    );

endmodule
