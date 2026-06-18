// Test case: a net driven by the parent's own always_comb but consumed by an
// instance input is internal. slang-autos must NOT promote it to an input port
// -- it belongs in AUTOLOGIC. This exercises the "internally driven" direction
// of the read/write classifier (the complement of the bit-select read case).
//
//   ctrl is written (LHS) inside always_comb and fed to u_sink.ctrl.
module top (
    input logic clk
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    always_comb begin
        ctrl = clk ? 4'hA : 4'h5;
    end

    sink u_sink (/*AUTOINST*/);

endmodule
