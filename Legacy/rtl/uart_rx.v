module uart_rx #(
    parameter BAUD_TIMER = 277
)(
    input  wire clk,
    input  wire rx_pin,
    output reg [7:0] rx_data,
    output reg rx_done
);
    reg [8:0] uart_timer;
    reg [3:0] uart_bit_idx;
    reg [1:0] uart_rx_sync;
    reg uart_rx_busy;

    always @(posedge clk) begin
        uart_rx_sync <= {uart_rx_sync[0], rx_pin};
        rx_done <= 0;
        
        if (!uart_rx_busy) begin
            if (uart_rx_sync[1] == 0) begin 
                uart_rx_busy <= 1;
                uart_timer <= BAUD_TIMER / 2; 
                uart_bit_idx <= 0;
            end
        end else begin
            if (uart_timer == 0) begin
                uart_timer <= BAUD_TIMER;
                if (uart_bit_idx == 8) begin
                    uart_rx_busy <= 0;
                    rx_done <= 1; 
                end else begin
                    rx_data <= {uart_rx_sync[1], rx_data[7:1]};
                    uart_bit_idx <= uart_bit_idx + 1;
                end
            end else begin
                uart_timer <= uart_timer - 1;
            end
        end
    end
endmodule