// Test case: User-defined ports should be preserved by AUTOPORTS
// some_sig is user-defined and also connected between submodule I/Os
module top (
    output logic [2:0] some_sig,
    /*AUTOPORTS*/
);
    /*AUTOWIRE*/

    // Producer drives some_sig
    producer u_producer (/*AUTOINST*/);

    // Consumer receives some_sig - this makes it "internal" from aggregator's view
    // but user explicitly declared it as a port, so it should remain a port
    consumer u_consumer (/*AUTOINST*/);
endmodule
