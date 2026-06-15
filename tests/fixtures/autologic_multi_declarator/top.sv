// Regression: a single declaration that declares several signals
// (`wire wire_a, wire_b;`) must record EVERY declared name as existing, not
// just the first declarator. wire_a and wire_b are internal nets here (driven
// by producer, consumed by consumer); both are user-declared, so AUTOLOGIC
// must not re-declare either of them.
module top (
    input logic clk
);

    wire wire_a, wire_b;

    /*AUTOLOGIC*/

    producer u_p (
        /*AUTOINST*/
    );

    consumer u_c (
        /*AUTOINST*/
    );

endmodule
