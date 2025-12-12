// Test case: User-defined ports should be preserved by AUTOPORTS
// some_sig is user-defined and also connected between submodule I/Os
module top(
output logic [2:0] some_sig,
/*AUTOPORTS*/
output logic [7:0] data_out,
input logic [7:0] data_in,
input logic rst_n,
input logic clk
);
    /*AUTOWIRE*/

    // Producer drives some_sig
    producer u_producer (/*AUTOINST*/
        // Outputs
        .some_sig (some_sig),
        .data_out (data_out),
        // Inputs
        .clk      (clk),
        .rst_n    (rst_n)
    );

    // Consumer receives some_sig - this makes it "internal" from aggregator's view
    // but user explicitly declared it as a port, so it should remain a port
    consumer u_consumer (/*AUTOINST*/
        // Inputs
        .clk      (clk),
        .rst_n    (rst_n),
        .some_sig (some_sig),
        .data_in  (data_in)
    );
endmodule
