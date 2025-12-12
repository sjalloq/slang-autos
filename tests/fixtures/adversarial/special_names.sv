// Test special/unusual instance names
module special_names (
    input  logic clk,
    input  logic rst_n
);

    // Instance names with numbers
    submod u_sub_0 (/*AUTOINST*/);
    submod u_sub_123 (/*AUTOINST*/);

    // Instance name with underscores
    submod u__double__underscore (/*AUTOINST*/);

    // Very long instance name
    submod u_this_is_a_very_long_instance_name_that_might_cause_formatting_issues (/*AUTOINST*/);

endmodule
