module top (
    input  wire MCU_CLK,
    input  wire SPI_CS,
    input  wire SPI_SCK,
    input  wire SPI_MOSI,
    output wire SPI_MISO
);

// --- 1. Async Input Synchronization (Requires 8x Oversampling) ---
reg [2:0] cs_sync;
reg [2:0] sck_sync;
reg [1:0] mosi_sync;

always @(posedge MCU_CLK) begin
    cs_sync   <= {cs_sync[1:0], SPI_CS};
    sck_sync  <= {sck_sync[1:0], SPI_SCK};
    mosi_sync <= {mosi_sync[0], SPI_MOSI};
end

wire cs_active   = ~cs_sync[2]; 
wire sck_falling = (sck_sync[2:1] == 2'b10); // Shift Edge (Mode 3)
wire sck_rising  = (sck_sync[2:1] == 2'b01); // Sample Edge (Mode 3)
wire mosi_data   = mosi_sync[1];

// --- 2. SPI State Machine ---
reg [2:0] bit_cnt;
reg [7:0] byte_cnt;
reg [7:0] shift_in;
reg [7:0] shift_out;

reg [7:0] rx_cmd;
reg [7:0] rx_p0;
reg [7:0] reg_file [0:15];

// Initialize Test Registers (R0=0, R1=1... R15=15)
integer i;
initial begin
    for(i = 0; i < 16; i = i + 1) reg_file[i] = i;
end

// --- 3. Dynamic TX Routing (Look-ahead block) ---
reg [7:0] next_tx_byte;

always @(*) begin
    if      (byte_cnt == 0) next_tx_byte = 8'h5A;  // Byte 0: Sync
    else if (byte_cnt == 1) next_tx_byte = rx_cmd; // Byte 1: Echo CMD
    else if (byte_cnt == 2) next_tx_byte = 8'h00;  // Byte 2: Len LSB
    else if (byte_cnt == 3) next_tx_byte = 8'h00;  // Byte 3: Len MSB
    else if (byte_cnt == 4) begin                  // Byte 4: Payload [0]
        if      (rx_cmd == 8'h01) next_tx_byte = 8'hAA; // PING
        else if (rx_cmd == 8'h02) next_tx_byte = 8'hDE; // READ ID
        else if (rx_cmd == 8'h04) next_tx_byte = rx_p0; // READ REG Echo Addr
        else                      next_tx_byte = 8'h00;
    end
    else if (byte_cnt == 5) begin                  // Byte 5: Payload [1]
        if      (rx_cmd == 8'h01) next_tx_byte = 8'hBB; // PING
        else if (rx_cmd == 8'h02) next_tx_byte = 8'hAD; // READ ID
        else if (rx_cmd == 8'h04) next_tx_byte = reg_file[rx_p0[3:0]]; // READ REG Data
        else                      next_tx_byte = 8'h00;
    end
    else next_tx_byte = 8'h00;
end

// --- 4. Core Shift / Sample Logic ---
always @(posedge MCU_CLK) begin
    if (!cs_active) begin
        bit_cnt   <= 0;
        byte_cnt  <= 0;
        shift_out <= 8'h5A; // Immediately load Sync byte while idle
    end else begin
        
        // SAMPLE on Rising Edge
        if (sck_rising) begin
            shift_in <= {shift_in[6:0], mosi_data};
            
            if (bit_cnt == 7) begin
                bit_cnt  <= 0;
                byte_cnt <= byte_cnt + 1;
                
                // Process fully received bytes
                case (byte_cnt)
                    1: rx_cmd <= {shift_in[6:0], mosi_data}; // CMD
                    4: rx_p0  <= {shift_in[6:0], mosi_data}; // Addr
                    5: if (rx_cmd == 8'h03) reg_file[rx_p0[3:0]] <= {shift_in[6:0], mosi_data}; // WR Data
                endcase
            end else begin
                bit_cnt <= bit_cnt + 1;
            end
        end
        
        // SHIFT on Falling Edge
        if (sck_falling) begin
            if (bit_cnt == 0) begin
                // Load the new byte on the first shift edge
                shift_out <= next_tx_byte;
            end else begin
                // Shift bits out sequentially
                shift_out <= {shift_out[6:0], 1'b0};
            end
        end
    end
end

assign SPI_MISO = cs_active ? shift_out[7] : 1'bz;

endmodule