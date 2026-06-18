// Test case: a sub-module output used only as a bit-select INDEX inside an
// always_comb block is consumed internally. slang-autos must NOT promote it
// to an external output port -- it belongs in AUTOLOGIC.
//
// Mirrors the real-world bug where `phy_shared_clkreq_sel` (an instance output)
// was incorrectly added to the parent's port list because procedural-block
// consumption (and bit-select index identifiers) were not tracked.
//
//   sel_idx is read as the index of data_in[sel_idx] inside always_comb.
//   It is driven by u_producer's output but consumed only by procedural logic.
module top (
    input  logic [3:0] data_in
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    logic [3:0] mux_out;

    always_comb begin : sel_mux
        for (int i = 0; i < 4; i++) begin
            mux_out[i] = data_in[sel_idx];
        end
    end

    producer u_producer (/*AUTOINST*/);

endmodule
