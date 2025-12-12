// Test modules with only inputs or only outputs
module unidirectional (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    output logic        valid,
    output logic [7:0]  data_out
);

    // Module with only inputs (sink)
    inputs_only u_sink (/*AUTOINST*/);

    // Module with only outputs (source)
    outputs_only u_source (/*AUTOINST*/);

endmodule
