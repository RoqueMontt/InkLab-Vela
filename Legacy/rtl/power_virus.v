module power_virus #(
    parameter NUM_LFSR = 75
)(
    input  wire clk,
    input  wire fast_toggle,
    output wire virus_reduction
);
    // A. Massive Logic Cell (LC) Array
    reg [31:0] lfsr [0:NUM_LFSR-1];
    integer i;
    initial begin
        for (i = 0; i < NUM_LFSR; i = i + 1) begin
            lfsr[i] = 32'h13579BDF ^ (i * 32'h08102040);
        end
    end

    always @(posedge clk) begin
        for (i = 0; i < NUM_LFSR; i = i + 1) begin
            lfsr[i] <= {lfsr[i][30:0], 1'b0} ^ (lfsr[i][31] ? 32'h80200003 : 32'h00000000);
        end
    end

    // B. Block RAM (EBR) Stress
    wire [15:0] bram_out_w [0:19];
    genvar k;
    generate
        for (k = 0; k < 20; k = k + 1) begin : bram_virus
            reg [15:0] ram [0:255];
            reg [7:0] addr = 0;
            reg [15:0] read_data = 0;
            always @(posedge clk) begin
                addr <= addr + 8'd7 + k;
                ram[addr] <= ~read_data ^ {16{fast_toggle}};
                read_data <= ram[addr];
            end
            assign bram_out_w[k] = read_data;
        end
    endgenerate

    // C. SPRAM Stress
    wire [15:0] spram_out_w [0:3];
    generate
        for (k = 0; k < 4; k = k + 1) begin : spram_virus
            reg [13:0] s_addr = 0;
            always @(posedge clk) s_addr <= s_addr + 14'd123 + k; 
            
            SB_SPRAM256KA spram_inst (
                .ADDRESS( s_addr ),
                .DATAIN( {16{fast_toggle}} ^ s_addr ),
                .MASKWREN( 4'b1111 ),
                .WREN( 1'b1 ),
                .CHIPSELECT( 1'b1 ),
                .CLOCK( clk ),
                .STANDBY( 1'b0 ),
                .SLEEP( 1'b0 ),
                .POWEROFF( 1'b1 ),
                .DATAOUT( spram_out_w[k] )
            );
        end
    endgenerate

    // D. Pipelined XOR Reduction Tree 
    reg [NUM_LFSR-1:0] lfsr_reduce;
    reg [19:0] bram_reduce;
    reg [3:0]  spram_reduce;

    integer j;
    always @(posedge clk) begin
        for (j = 0; j < NUM_LFSR; j = j + 1) lfsr_reduce[j] <= ^lfsr[j];
        for (j = 0; j < 20;        j = j + 1) bram_reduce[j] <= ^bram_out_w[j];
        for (j = 0; j < 4;         j = j + 1) spram_reduce[j] <= ^spram_out_w[j];
    end

    assign virus_reduction = (^lfsr_reduce) ^ (^bram_reduce) ^ (^spram_reduce);

endmodule