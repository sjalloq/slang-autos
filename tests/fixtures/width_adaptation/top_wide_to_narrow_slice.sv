// Test: wide producer + narrow consumer. Signal is declared at the producer's
// width (16-bit) and the consumer's input is driven from a bit-slice.
module top_wide_to_narrow_slice;
    /*AUTOLOGIC*/

    producer_wide u_producer (/*AUTOINST*/);
    consumer_narrow u_consumer (/*AUTOINST*/);
endmodule
