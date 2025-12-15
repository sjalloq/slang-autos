// Test: zero-pad input when port > signal width
// producer_narrow produces 8-bit, consumer_wide expects 16-bit
// Expected: consumer input gets {'0, data}
module top_pad_input;
    /*AUTOLOGIC*/

    producer_narrow u_producer (/*AUTOINST*/);
    consumer_wide u_consumer (/*AUTOINST*/);
endmodule
