// Reproduces: `assign` inside a dead generate branch must not leak into
// AUTOLOGIC/AUTOPORTS classification. MODE defaults to 0 so the
// `if (MODE == 1)` branch is elaboration-dead.
module top #(
    parameter integer MODE = 0
) (
    input logic clk,
    /*AUTOPORTS*/
);

    /*AUTOLOGIC*/

    // The child lives at module scope so its ports are always elaborated.
    child u_child (
        /*AUTOINST*/
    );

    if (MODE == 1) begin : g_mode_1
        // Dead by default - this assign references data_out (driven by u_child
        // in the live scope) and clk. Parser currently records data_out in
        // assign_driven even though the branch is dead, causing AUTOLOGIC to
        // redeclare data_out as internal logic on top of the AUTOPORTS output.
        assign data_out = clk;
    end

endmodule
