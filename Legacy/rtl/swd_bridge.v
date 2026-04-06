module swd_bridge #(
    parameter CLK_DIV = 5 // Adjust based on sys clk (e.g., 50MHz / (2 * 5) = 5MHz SWDCLK)
)(
    input  wire        clk,
    input  wire        rst_n,

    // Core Command Interface
    input  wire        req_start,
    input  wire        req_reset,
    input  wire        cmd_apndp,  // 0: DP, 1: AP
    input  wire        cmd_rw,     // 0: Write, 1: Read
    input  wire [1:0]  cmd_addr,   // A[3:2]
    input  wire [31:0] wdata,
    
    output reg  [31:0] rdata,
    output reg  [2:0]  ack,        // 3'b001 = OK
    output reg         error,      // Parity or ACK error
    output reg         busy,

    // Physical SWD interface
    output reg         swd_clk,
    output reg         swd_dio_out,
    input  wire        swd_dio_in,
    output reg         swd_dio_oe
);

    // Clock divider for SWD
    reg [7:0] clk_cnt;
    wire tick = (clk_cnt == 0);
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            clk_cnt <= CLK_DIV;
            swd_clk <= 0;
        end else if (tick) begin
            clk_cnt <= CLK_DIV;
            swd_clk <= ~swd_clk; // Toggle SWD clock
        end else clk_cnt <= clk_cnt - 1;
    end

    // Ticks to drive logic (Drive on falling, Sample on rising)
    wire tick_f = tick && swd_clk;  // About to go low
    wire tick_r = tick && !swd_clk; // About to go high

    // FSM States
    localparam S_IDLE     = 4'd0,
               S_RESET    = 4'd1,
               S_REQ      = 4'd2,
               S_TRN1     = 4'd3,
               S_ACK      = 4'd4,
               S_TRN2_WR  = 4'd5,
               S_DATA_WR  = 4'd6,
               S_DATA_RD  = 4'd7,
               S_TRN2_RD  = 4'd8;

    reg [3:0]  state;
    reg [6:0]  bit_cnt;
    reg [7:0]  hdr;
    reg [32:0] shift_reg; 

    wire parity_data = ^wdata;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state       <= S_IDLE;
            busy        <= 0;
            error       <= 0;
            swd_dio_oe  <= 0;
            swd_dio_out <= 1;
            ack         <= 0;
        end else begin
            case (state)
                S_IDLE: begin
                    busy <= 0;
                    swd_dio_oe <= 0;
                    if (req_reset) begin
                        busy <= 1;
                        bit_cnt <= 63; // 64 bits for line reset
                        state <= S_RESET;
                    end else if (req_start) begin
                        busy <= 1;
                        error <= 0;
                        bit_cnt <= 7;
                        // Build Header (LSB first): Start(1), APnDP, RW, A2, A3, Parity, Stop(0), Park(1)
                        hdr[0]   <= 1'b1;
                        hdr[1]   <= cmd_apndp;
                        hdr[2]   <= cmd_rw;
                        hdr[4:3] <= cmd_addr;
                        hdr[5]   <= cmd_apndp ^ cmd_rw ^ cmd_addr[0] ^ cmd_addr[1]; // Parity
                        hdr[6]   <= 1'b0;
                        hdr[7]   <= 1'b1;
                        state    <= S_REQ;
                    end
                end

                S_RESET: begin
                    if (tick_f) begin
                        swd_dio_oe <= 1;
                        // Send 56 '1's followed by 8 '0's (satisfies line reset & idle)
                        swd_dio_out <= (bit_cnt > 7); 
                        if (bit_cnt == 0) state <= S_IDLE;
                        else bit_cnt <= bit_cnt - 1;
                    end
                end

                S_REQ: begin
                    if (tick_f) begin
                        swd_dio_oe  <= 1;
                        swd_dio_out <= hdr[0];
                        hdr         <= {1'b0, hdr[7:1]};
                        if (bit_cnt == 0) state <= S_TRN1;
                        else bit_cnt <= bit_cnt - 1;
                    end
                end

                S_TRN1: begin // Turnaround (Z-state)
                    if (tick_f) begin
                        swd_dio_oe <= 0;
                        bit_cnt <= 2;
                        state <= S_ACK;
                    end
                end

                S_ACK: begin // Sample ACK from target
                    if (tick_r) begin
                        ack <= {swd_dio_in, ack[2:1]}; // LSB first
                        if (bit_cnt == 0) begin
                            // ack val checking happens slightly delayed, check combinationally:
                            if ({swd_dio_in, ack[2:1]} != 3'b001) begin
                                error <= 1;
                                state <= S_IDLE; // Abort if not OK
                            end else if (cmd_rw) begin
                                bit_cnt <= 32;
                                state <= S_DATA_RD;
                            end else begin
                                state <= S_TRN2_WR;
                            end
                        end else bit_cnt <= bit_cnt - 1;
                    end
                end

                S_TRN2_WR: begin // Host takes over for Write
                    if (tick_f) begin
                        swd_dio_oe <= 1;
                        shift_reg <= {parity_data, wdata};
                        bit_cnt <= 32;
                        state <= S_DATA_WR;
                    end
                end

                S_DATA_WR: begin
                    if (tick_f) begin
                        swd_dio_out <= shift_reg[0];
                        shift_reg   <= {1'b0, shift_reg[32:1]};
                        if (bit_cnt == 0) state <= S_IDLE;
                        else bit_cnt <= bit_cnt - 1;
                    end
                end

                S_DATA_RD: begin
                    if (tick_r) begin
                        shift_reg <= {swd_dio_in, shift_reg[32:1]};
                        if (bit_cnt == 0) state <= S_TRN2_RD;
                        else bit_cnt <= bit_cnt - 1;
                    end
                end

                S_TRN2_RD: begin
                    if (tick_f) begin // Check Parity & end
                        swd_dio_oe <= 0;
                        rdata <= shift_reg[31:0];
                        if (shift_reg[32] != ^shift_reg[31:0]) error <= 1;
                        state <= S_IDLE;
                    end
                end
            endcase
        end
    end
endmodule