module top (
    /*AUTOPORTS*/
);

    // User-declared signal (should be skipped by AUTOWIRE)
    logic user_declared_wire;

    /*AUTOWIRE*/

    producer u_producer (/*AUTOINST*/);
    consumer u_consumer (/*AUTOINST*/);

endmodule
