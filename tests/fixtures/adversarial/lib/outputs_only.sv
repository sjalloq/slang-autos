// Module with only output ports (source)
module outputs_only (
    output logic        valid,
    output logic [7:0]  data_out
);
    assign valid = 1'b1;
    assign data_out = 8'hAB;
endmodule
