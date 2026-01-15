// Test for instance arrays - verifies that module lookup works
// when instances are declared as arrays (e.g., inst[2:0])
module top (
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    // Instance array - this tests that InstanceArraySymbol is handled
    sync_pulse sync_arr[2:0] (/*AUTOINST*/);

endmodule
