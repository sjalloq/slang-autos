module top (output logic d /*AUTOPORTS*/);

    /*AUTOWIRE*/
    // Beginning of automatic wires
    logic [3:0] c_1;
    logic [3:0] c_0;
    // End of automatics

    /*AUTOREG*/
    
    /* submod AUTO_TEMPLATE 
        a => c_math.modulo(@,2)
        b => _
        c => c_@
    */
    submod #(.WIDTH (4)) u_submod_0 (/*AUTOINST*/
    // Outputs
    .b     (),
    .c     (c_0),
    // Inputs
    .clk   (clk),
    .rst_n (rst_n),
    .a     (c_math.modulo(0,2))
);
    submod #(.WIDTH (3)) u_submod_1 (/*AUTOINST*/
    // Outputs
    .b     (),
    .c     (c_1),
    // Inputs
    .clk   (clk),
    .rst_n (rst_n),
    .a     (c_math.modulo(1,2))
);

endmodule