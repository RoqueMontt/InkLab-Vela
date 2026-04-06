module rgb_driver (
    input wire clk,
    input wire [7:0] r,
    input wire [7:0] g,
    input wire [7:0] b,
    input wire mute,
    
    output wire rgb0,
    output wire rgb1,
    output wire rgb2
);
    reg [7:0] pwm_counter;
    always @(posedge clk) pwm_counter <= pwm_counter + 1;

    wire pwm_r = ~mute & (pwm_counter < r);
    wire pwm_g = ~mute & (pwm_counter < g);
    wire pwm_b = ~mute & (pwm_counter < b);

    SB_RGBA_DRV #(
        .CURRENT_MODE("0b1"),
        .RGB0_CURRENT("0b000011"), 
        .RGB1_CURRENT("0b000011"), 
        .RGB2_CURRENT("0b000011")  
    ) RGB_DRV_INST (
        .CURREN(1'b1),
        .RGBLEDEN(1'b1),
        .RGB0PWM(pwm_r),
        .RGB1PWM(pwm_g),
        .RGB2PWM(pwm_b),
        .RGB0(rgb0),
        .RGB1(rgb1),
        .RGB2(rgb2)
    );
endmodule