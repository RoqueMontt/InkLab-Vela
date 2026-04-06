module top (
    input  wire MCU_CLK,
    // Keep SPI inputs but ignore them
    input  wire SPI_CS, SPI_SCK, SPI_MOSI,
    output wire SPI_MISO,
    
    // All outputs must be tied to a static 0 to prevent floating gate leakage
    output wire CON_02, output wire CON_03, output wire CON_04, output wire CON_05,
    output wire CON_06, output wire CON_07, output wire CON_08, output wire CON_09,
    output wire CON_10, output wire CON_11, output wire CON_12, output wire CON_13,
    output wire CON_14, output wire CON_15, output wire CON_16, output wire CON_17,
    output wire EPD_D0, output wire EPD_D1, output wire EPD_D2, output wire EPD_D3,
    output wire EPD_D4, output wire EPD_D5, output wire EPD_D6, output wire EPD_D7,
    output wire EPD_XCL, output wire EPD_LEH, output wire EPD_MODE,
    output wire EPD_CKV, output wire EPD_XSTL, output wire EPD_OE, output wire EPD_STV
);

    // Disable MISO (High-Z) so it doesn't fight the MCU
    assign SPI_MISO = 1'bz;

    // Tie ALL output pins to GND (0)
    // This removes all dynamic toggling and keeps the buffers in a static state
    assign {CON_02, CON_03, CON_04, CON_05, CON_06, CON_07, CON_08, CON_09,
            CON_10, CON_11, CON_12, CON_13, CON_14, CON_15, CON_16, CON_17,
            EPD_D0, EPD_D1, EPD_D2, EPD_D3, EPD_D4, EPD_D5, EPD_D6, EPD_D7,
            EPD_XCL, EPD_LEH, EPD_MODE, EPD_CKV, EPD_XSTL, EPD_OE, EPD_STV} = 31'b0;

    // Synthesis directive: remove unused logic
    // By not instantiating any registers or LUTs, Yosys/nextpnr will 
    // optimize away the entire design into essentially a "wire-only" netlist.
    
endmodule