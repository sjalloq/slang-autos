// Regression: the `// End of automatics` marker that closes an existing
// AUTOLOGIC block sits in the leading trivia of the generate construct that
// immediately follows it. The marker detection used to run *after* the
// generate-construct dispatch (which early-returns), so `block_end` was never
// set, the writer skipped the re-expansion (start > end), and the AUTOLOGIC
// block was never regenerated. Here the block starts empty: `data_int`
// (driven by u_p, consumed by u_c, both in the live scope) must be regenerated.
module top (
    input  logic clk,
    output logic done_o
);

    /*AUTOLOGIC*/
    // Beginning of automatic logic
    // End of automatics

    if (1) begin : g_blk
        producer u_p (
            /*AUTOINST*/
        );
        consumer u_c (
            /*AUTOINST*/
        );
    end

endmodule
