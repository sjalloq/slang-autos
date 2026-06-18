// Producer module driving two outputs:
//   sel_idx   - used only as a bit-select INDEX inside the parent's always_comb
//               (consumed internally -> must NOT become an external output port)
//   prod_data - not consumed anywhere in the parent (genuine external output)
module producer (
    input  logic       clk,
    output logic [1:0] sel_idx,
    output logic [7:0] prod_data
);
endmodule
