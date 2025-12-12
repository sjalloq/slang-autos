module top (
    /*AUTOPORTS*/
);

    // User-declared signal (should be skipped by AUTOLOGIC)
    logic user_declared_wire;

    /*AUTOLOGIC*/

    producer u_producer (/*AUTOINST*/);
    consumer u_consumer (/*AUTOINST*/);

endmodule
