// Test: AUTOLOGIC with parameterized port widths
// internal_data should be declared with resolved width [7:0] (WIDTH=8 default)
module top_autologic(
    input logic clk
);
    /*AUTOLOGIC*/

    // submod has WIDTH=8 by default
    // u_producer.data_out -> internal_data -> u_consumer.data_in
    /* submod AUTO_TEMPLATE
       data_out => internal_data,
       data_in => internal_data,
    */
    submod u_producer (
        .clk(clk),
        .data_in('0)
        /*AUTOINST*/
    );

    submod u_consumer (
        .clk(clk),
        .data_out()
        /*AUTOINST*/
    );
endmodule
