module spi_slave (
    input  wire clk,
    input  wire rst_n, // Added reset for robust operation
    
    // Physical SPI
    input  wire spi_cs,
    input  wire spi_sck,
    input  wire spi_mosi,
    output wire spi_miso,
    output wire spi_miso_oe, // Replaced high-Z assign with OE for FPGA standard compliance
    
    // UART Integration
    input  wire uart_rx_done,
    input  wire [7:0] uart_rx_data,
    
    // Core Registers Out
    output reg [15:0] gpio_dir,
    output reg [15:0] gpio_out,
    input  wire [15:0] gpio_in,
    
    output reg [7:0] pwm_duty,
    output reg [7:0] rgb_r,
    output reg [7:0] rgb_g,
    output reg [7:0] rgb_b,
    output reg rgb_mute,
    output reg stress_en,

    // SWD Interface signals to swd_bridge
    output reg         swd_req_start,
    output reg         swd_req_reset,
    output reg         swd_cmd_apndp,
    output reg         swd_cmd_rw,
    output reg [1:0]   swd_cmd_addr,
    output reg [31:0]  swd_wdata,
    input  wire [31:0] swd_rdata,
    input  wire [2:0]  swd_ack,
    input  wire        swd_error,
    input  wire        swd_busy
);

    // --- Async Input Synchronization ---
    reg [2:0] cs_sync;
    reg [2:0] sck_sync;
    reg [2:0] mosi_sync; // Expanded to 3 stages

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cs_sync <= 3'b111;
            sck_sync <= 3'b000;
            mosi_sync <= 3'b000;
        end else begin
            cs_sync   <= {cs_sync[1:0], spi_cs};
            sck_sync  <= {sck_sync[1:0], spi_sck};
            mosi_sync <= {mosi_sync[1:0], spi_mosi};
        end
    end

    wire cs_active   = ~cs_sync[2]; 
    wire sck_falling = (sck_sync[2:1] == 2'b10); 
    wire sck_rising  = (sck_sync[2:1] == 2'b01); 
    wire mosi_data   = mosi_sync[2]; 

    // --- SPI State & FIFO ---
    reg [2:0] bit_cnt;
    reg [7:0] byte_cnt;
    reg [7:0] shift_in, shift_out;
    reg [7:0] rx_cmd, rx_p0, rx_p1;

    reg [7:0] fifo_mem [0:15];
    reg [3:0] fifo_head, fifo_tail;
    reg [7:0] fifo_read_val;

    wire spi_fifo_write = (sck_rising && bit_cnt == 7 && byte_cnt == 5 && rx_cmd == 8'h0A);

    always @(posedge clk) begin
        if (uart_rx_done) begin
            fifo_mem[fifo_head] <= uart_rx_data;
            fifo_head <= fifo_head + 1;
        end else if (spi_fifo_write) begin
            fifo_mem[fifo_head] <= {shift_in[6:0], mosi_data};
            fifo_head <= fifo_head + 1;
        end
        fifo_read_val <= fifo_mem[fifo_tail];
    end

    // --- Dynamic TX Routing ---
    reg [7:0] next_tx_byte;
    always @(*) begin
        case(byte_cnt)
            0: next_tx_byte = 8'h5A;
            1: next_tx_byte = rx_cmd;
            4: begin                  
                if      (rx_cmd == 8'h07) next_tx_byte = gpio_in[15:8];
                else if (rx_cmd == 8'h0B) next_tx_byte = fifo_read_val;
                // SWD Extensions
                else if (rx_cmd == 8'h24) next_tx_byte = swd_rdata[15:8];
                else if (rx_cmd == 8'h25) next_tx_byte = swd_rdata[31:24];
                else if (rx_cmd == 8'h26) next_tx_byte = {5'b0, swd_ack}; // Status ACK
                else                      next_tx_byte = 8'h00;
            end
            5: begin                  
                if      (rx_cmd == 8'h07) next_tx_byte = gpio_in[7:0];
                else if (rx_cmd == 8'h24) next_tx_byte = swd_rdata[7:0];
                else if (rx_cmd == 8'h25) next_tx_byte = swd_rdata[23:16];
                else if (rx_cmd == 8'h26) next_tx_byte = {6'b0, swd_error, swd_busy};
                else                      next_tx_byte = 8'h00;
            end
            default: next_tx_byte = 8'h00;
        endcase
    end

    // --- Core Shift Logic ---
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            bit_cnt       <= 0;
            byte_cnt      <= 0;
            shift_out     <= 8'h5A;
            swd_req_start <= 0;
            swd_req_reset <= 0;
            fifo_head     <= 0;
            fifo_tail     <= 0;
        end else if (!cs_active) begin
            bit_cnt       <= 0;
            byte_cnt      <= 0;
            shift_out     <= 8'h5A; // Pre-load first byte
            swd_req_start <= 0;     // Ensure pulse clears
            swd_req_reset <= 0;
        end else begin
            // Clear pulses safely while CS is active
            if (swd_req_start) swd_req_start <= 0; 
            if (swd_req_reset) swd_req_reset <= 0;

            if (sck_rising) begin
                shift_in <= {shift_in[6:0], mosi_data};
                if (bit_cnt == 7) begin
                    bit_cnt  <= 0;
                    byte_cnt <= byte_cnt + 1;
                    
                    case (byte_cnt)
                        1: rx_cmd <= {shift_in[6:0], mosi_data};
                        4: rx_p0  <= {shift_in[6:0], mosi_data};
                        5: begin
                            rx_p1 <= {shift_in[6:0], mosi_data};
                            
                            // Existing Core Cmds
                            if (rx_cmd == 8'h05) gpio_dir  <= {rx_p0, {shift_in[6:0], mosi_data}};
                            if (rx_cmd == 8'h06) gpio_out  <= {rx_p0, {shift_in[6:0], mosi_data}};
                            if (rx_cmd == 8'h0B) fifo_tail <= fifo_tail + 1;
                            if (rx_cmd == 8'h0C) pwm_duty  <= rx_p0;
                            if (rx_cmd == 8'h0D) rgb_r     <= rx_p0;
                            if (rx_cmd == 8'h0E) rgb_g     <= rx_p0; 
                            if (rx_cmd == 8'h0F) rgb_b     <= rx_p0; 
                            if (rx_cmd == 8'h10) rgb_mute  <= rx_p0[0];
                            if (rx_cmd == 8'h11) stress_en <= rx_p0[0]; 
                            
                            // --- New SWD Integrations ---
                            if (rx_cmd == 8'h20) begin
                                swd_cmd_addr  <= rx_p0[3:2];
                                swd_cmd_apndp <= rx_p0[1];
                                swd_cmd_rw    <= rx_p0[0];
                            end
                            if (rx_cmd == 8'h21) swd_wdata[15:0]  <= {rx_p0, {shift_in[6:0], mosi_data}};
                            if (rx_cmd == 8'h22) swd_wdata[31:16] <= {rx_p0, {shift_in[6:0], mosi_data}};
                            if (rx_cmd == 8'h23) begin
                                if (rx_p0[0]) swd_req_reset <= 1;
                                else          swd_req_start <= 1;
                            end
                        end
                    endcase
                end else bit_cnt <= bit_cnt + 1;
            end
            
            if (sck_falling) begin
                if (bit_cnt == 0) shift_out <= next_tx_byte;
                else              shift_out <= {shift_out[6:0], 1'b0};
            end
        end
    end

    // Replaced Tri-state with OE for FPGA fabric standards
    assign spi_miso    = shift_out[7];
    assign spi_miso_oe = cs_active;

endmodule