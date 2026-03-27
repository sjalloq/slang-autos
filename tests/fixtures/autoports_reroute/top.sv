module top (
    input  logic clk
    /*AUTOPORTS*/,
    output logic  [7:0] data_out,
    input logic  [7:0] data_in
);
    /*AUTOWIRE*/
    /*AUTOLOGIC*/

    logic [7:0] data_in;
    assign data_in = data_out;

    producer u_prod (/*AUTOINST*/
        // Outputs
        .data_out (data_out),
        // Inputs
        .clk      (clk)
    );
    consumer u_cons (/*AUTOINST*/
        // Inputs
        .clk     (clk),
        .data_in (data_in)
    );
endmodule
