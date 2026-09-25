// Test case: instances inside generate branches selected by a parameter
// override (-G MODE=...), with trailing per-port comments before /*AUTOINST*/.
// The last manual port already ends with a comma, so no comma must be inserted
// after the marker in either branch.
module top #(
    parameter bit MODE = 0
) (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    if (MODE) begin : g_mode_1
        /* submod AUTO_TEMPLATE
           .* => port.input ? 0 : _  // Unused
        */
        submod u_sub (
            .clk                         (clk),                       // input
            .data_out                    (data_out),                  // output
            /*AUTOINST*/
        );
    end else begin : g_mode_0
        submod u_sub (
            .clk                         (clk),                       // input
            .data_out                    (data_out),                  // output
            /*AUTOINST*/
        );
    end
endmodule
