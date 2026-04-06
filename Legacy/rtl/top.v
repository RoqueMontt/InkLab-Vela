module top (
    input  wire MCU_CLK,
    input  wire SPI_CS,
    input  wire SPI_SCK,
    input  wire SPI_MOSI,
    output wire SPI_MISO,
    
    // Dedicated RGB LED Pins
    output wire RGB0, // Physical Red
    output wire RGB1, // Physical Green
    output wire RGB2, // Physical Blue
    
    // Generic GPIOs (Reduced by 2 to make room for SWD)
    inout wire CON_02, inout wire CON_03, inout wire CON_04, inout wire CON_05,
    inout wire CON_06, inout wire CON_07, inout wire CON_08, inout wire CON_09,
    inout wire CON_10, inout wire CON_11, inout wire CON_12, 
    
    // Repurposed for SWD Interface
    output wire CON_13, // Assigned to SWD_CLK
    inout  wire CON_14, // Assigned to SWD_DIO
    
    // Dedicated Test Pins
    input  wire CON_15, // UART RX (From MCU PB2)
    output wire CON_16, // UART TX (To MCU PB0)
    output wire CON_17  // PWM Out (To MCU PB1 ADC)
);

    // --- Power-on Reset Generation ---
    reg [3:0] reset_cnt = 0;
    wire rst_n = &reset_cnt; // Goes high when count reaches 15
    always @(posedge MCU_CLK) begin
        if (!rst_n) reset_cnt <= reset_cnt + 1;
    end

    // --- Internal Bus Signals ---
    wire [15:0] gpio_dir;
    wire [15:0] gpio_out;
    wire [10:0] gpio_in; // Now 11 actual mapped pins
    
    wire [7:0]  pwm_duty;
    wire [7:0]  rgb_r, rgb_g, rgb_b;
    wire        rgb_mute;
    wire        stress_en;
    
    wire [7:0]  uart_rx_data;
    wire        uart_rx_done;
    wire        virus_reduction;

    // --- SPI MISO Top-Level Tri-State Buffer ---
    wire spi_miso_data;
    wire spi_miso_oe;
    // This safely turns the pad off (High-Z) when CS is inactive
    assign SPI_MISO = spi_miso_oe ? spi_miso_data : 1'bz;

    // --- SWD Internal Signals ---
    wire        swd_req_start, swd_req_reset;
    wire        swd_cmd_apndp, swd_cmd_rw;
    wire [1:0]  swd_cmd_addr;
    wire [31:0] swd_wdata, swd_rdata;
    wire [2:0]  swd_ack;
    wire        swd_error, swd_busy;
    
    wire        swd_clk_out;
    wire        swd_dio_in;
    wire        swd_dio_out;
    wire        swd_dio_oe;

    // --- SWD Physical Pins Handling ---
    assign CON_13 = swd_clk_out; // Clock is strictly an output
    
    // Bidirectional IO buffer for SWD_DIO
    assign CON_14     = swd_dio_oe ? swd_dio_out : 1'bz;
    assign swd_dio_in = CON_14;

    // --- Fast Toggle Clock Divider (Shared resource) ---
    reg fast_toggle = 0;
    always @(posedge MCU_CLK) fast_toggle <= ~fast_toggle;

    // --- Module Instantiations ---

    spi_slave spi_inst (
        .clk(MCU_CLK),
        .rst_n(rst_n),
        
        .spi_cs(SPI_CS), .spi_sck(SPI_SCK), .spi_mosi(SPI_MOSI), 
        .spi_miso(spi_miso_data), .spi_miso_oe(spi_miso_oe),
        
        .uart_rx_done(uart_rx_done), .uart_rx_data(uart_rx_data),
        .gpio_dir(gpio_dir), .gpio_out(gpio_out), .gpio_in({5'b0, gpio_in}), // Pad unused to 16 bits
        .pwm_duty(pwm_duty),
        .rgb_r(rgb_r), .rgb_g(rgb_g), .rgb_b(rgb_b), .rgb_mute(rgb_mute),
        .stress_en(stress_en),

        // Connect SWD control lines to the bridge
        .swd_req_start(swd_req_start), .swd_req_reset(swd_req_reset),
        .swd_cmd_apndp(swd_cmd_apndp), .swd_cmd_rw(swd_cmd_rw),
        .swd_cmd_addr(swd_cmd_addr),
        .swd_wdata(swd_wdata), .swd_rdata(swd_rdata),
        .swd_ack(swd_ack), .swd_error(swd_error), .swd_busy(swd_busy)
    );

    // Instantiate our new SWD hardware bridge
    swd_bridge swd_bridge_inst (
        .clk(MCU_CLK),
        .rst_n(rst_n),
        
        .req_start(swd_req_start), .req_reset(swd_req_reset),
        .cmd_apndp(swd_cmd_apndp), .cmd_rw(swd_cmd_rw),
        .cmd_addr(swd_cmd_addr),
        .wdata(swd_wdata), .rdata(swd_rdata),
        .ack(swd_ack), .error(swd_error), .busy(swd_busy),

        // Connect physical IOs
        .swd_clk(swd_clk_out),
        .swd_dio_in(swd_dio_in),
        .swd_dio_out(swd_dio_out),
        .swd_dio_oe(swd_dio_oe)
    );

    gpio_ctrl gpio_inst (
        .stress_en(stress_en),
        .fast_toggle(fast_toggle),
        .dir(gpio_dir[10:0]),
        .out(gpio_out[10:0]),
        .in(gpio_in),
        // Pins 13 and 14 removed to use for SWD
        .pins({CON_12, CON_11, CON_10, CON_09, CON_08, CON_07, CON_06, CON_05, CON_04, CON_03, CON_02})
    );

    pwm_gen pwm_inst (
        .clk(MCU_CLK),
        .duty(pwm_duty),
        .stress_en(stress_en),
        .fast_toggle(fast_toggle),
        .pwm_out(CON_17)
    );

    uart_rx uart_rx_inst (
        .clk(MCU_CLK),
        .rx_pin(CON_15),
        .rx_data(uart_rx_data),
        .rx_done(uart_rx_done)
    );

    rgb_driver rgb_inst (
        .clk(MCU_CLK),
        .r(rgb_r), .g(rgb_g), .b(rgb_b), .mute(rgb_mute),
        .rgb0(RGB0), .rgb1(RGB1), .rgb2(RGB2)
    );

/*     power_virus virus_inst (
        .clk(MCU_CLK),
        .fast_toggle(fast_toggle),
        .virus_reduction(virus_reduction)
    ); */

    assign CON_16 = stress_en ? (fast_toggle ^ virus_reduction) : (1'b1 ^ virus_reduction);

endmodule