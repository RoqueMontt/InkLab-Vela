module gpio_ctrl (
    input  wire stress_en,
    input  wire fast_toggle,
    input  wire [12:0] dir,  // Direction mapped to CON_02...CON_14
    input  wire [12:0] out,  // Output values
    output wire [12:0] in,   // Readback values
    
    inout  wire [12:0] pins
);

    genvar i;
    generate
        for (i = 0; i < 13; i = i + 1) begin : gpio_gen
            assign pins[i] = stress_en ? fast_toggle : (dir[i] ? out[i] : 1'bz);
            assign in[i]   = pins[i];
        end
    endgenerate

endmodule