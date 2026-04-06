# Project Files
# Add all your Verilog files here, separated by spaces.
SRC = top.v \
      spi_slave.v \
      swd_bridge.v \
      gpio_ctrl.v \
      pwm_gen.v \
      uart_rx.v \
      rgb_driver.v \

PCF_FILE = pins.pcf

# Intermediate Files
JSON = hardware.json
ASC = hardware.asc
BIN = hardware.bin

# Tools
YOSYS = yosys
NEXTPNR = nextpnr-ice40
ICEPACK = icepack

# Default Target
all: $(BIN)

# Step 3: Pack ASC into BIN
$(BIN): $(ASC)
	$(ICEPACK) -s $(ASC) $(BIN)

# Step 2: Place and Route (JSON -> ASC)
# Changed --freq 12 to --freq 32 to ensure the router meets timing for 32 MHz!
$(ASC): $(JSON) $(PCF_FILE)
	$(NEXTPNR) --up5k --package sg48 --pcf $(PCF_FILE) --json $(JSON) --asc $(ASC) --freq 12

# Step 1: Synthesis (Verilog -> JSON)
# Yosys will now ingest all files listed in the $(SRC) variable
$(JSON): $(SRC)
	$(YOSYS) -p "synth_ice40 -top top -json $(JSON)" $(SRC)

# Clean up generated files
clean:
	@del /f $(JSON) $(ASC) $(BIN) 2>nul || rm -f $(JSON) $(ASC) $(BIN) 
	@echo Cleaned up intermediate files.

.PHONY: all clean