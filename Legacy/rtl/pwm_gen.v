module pwm_gen (
    input  wire clk,
    input  wire [7:0] duty,
    input  wire stress_en,
    input  wire fast_toggle,
    output wire pwm_out
);
    reg [7:0] counter;
    always @(posedge clk) counter <= counter + 1;
    
    assign pwm_out = stress_en ? fast_toggle : (counter < duty);
endmodule