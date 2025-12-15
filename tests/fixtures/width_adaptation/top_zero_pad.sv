// Test: zero-pad when input port > signal width
// narrow (8-bit output) produces signal, wide (16-bit input) consumes it
// Expected: wide input gets {'0, signal}
module top_zero_pad;
    /*AUTOLOGIC*/

    narrow u_narrow (/*AUTOINST*/);
    wide u_wide (
        .clk     (clk),
        .data_in (data_out),  // Connect narrow output to wide input
        .data_out(wide_out)   // separate output
        /*AUTOINST*/
    );
endmodule
