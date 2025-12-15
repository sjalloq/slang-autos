// Test: unused signal for output when port > signal width
// producer_wide produces 16-bit, consumer_narrow expects 8-bit
// Expected: data declared 16-bit, consumer gets data[7:0]
//           producer output gets {unused_data_u_producer, data} - NO WAIT
// Actually since producer is widest, signal is 16-bit.
// Consumer input needs slice.
// This is actually the slice case, not unused output.
module top_unused_output;
    /*AUTOLOGIC*/

    producer_wide u_producer (/*AUTOINST*/);
    consumer_narrow u_consumer (/*AUTOINST*/);
endmodule
