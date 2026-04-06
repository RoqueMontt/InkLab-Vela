// ============================================================================
// INCLUDES
// ============================================================================
#include "frontend_api.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_fatfs.h"
#include "ff.h"
#include "usbd_cdc_if.h"

// Hardware Drivers & Logic Modules
#include "bq25798.h"
#include "sys_power.h"
#include "led_manager.h"
#include "joystick.h"
#include "diagnostics.h"
#include "fpga.h"
#include "error_manager.h"
#include "pinmux.h"



// ============================================================================
// TYPE DEFINITIONS & STATE
// ============================================================================
typedef struct {
    uint8_t buffer_idx;
    uint16_t length;
} DiskWriteReq_t;


typedef enum { UPLOAD_IDLE, UPLOAD_RECEIVING } UploadState;

typedef void (*CommandHandler_t)(int argc, char **argv);
typedef struct { const char *cmd; CommandHandler_t handler; } CommandDef_t;


static osMessageQueueId_t DiskWriteQueue;
static osSemaphoreId_t BufferPoolSem;

__attribute__((aligned(4))) uint8_t sd_write_buffer[2][8192]; // Also used by Diagnostics
static uint8_t active_fill_buf = 0;
static uint16_t sd_write_idx = 0;

static UploadState current_upload_state = UPLOAD_IDLE;
static FIL upload_fil;
static uint32_t upload_bytes_total = 0;
static uint32_t upload_bytes_received = 0;
static uint16_t bytes_since_last_ack = 0;
static uint8_t incoming_checksum = 0;

static char cmd_buffer[128];
static uint16_t cmd_idx = 0;
static uint32_t last_rx_time = 0;

// External References
extern FATFS FatFs;
extern uint8_t current_slot;
extern uint8_t fpga_is_ready;
extern char slot_names[16][16];
extern uint8_t slot_clk_configs[16];
extern uint8_t last_active_slot;
extern volatile uint8_t sd_is_mounted;
extern volatile uint8_t telem_is_muted;
extern volatile uint32_t telem_rate_ms;
extern osMessageQueueId_t FpgaTxQueueHandle;

extern osThreadId_t FpgaTaskHandle;
void Frontend_ExecuteCommand(char* cmd_str);

extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart3;
extern HAL_StatusTypeDef OTA_Update_From_SD(const char* filename);

extern void USB_Printf(const char *format, ...);
extern void Save_Configs(void);
extern void Apply_Slot_Clock(uint8_t slot);
extern void Telemetry_SetFastTarget(uint8_t ch, const char* target);

// --- HELPER DECLARATION: Add this to led_manager.c manually if not present ---
// void LED_GetStatus(uint8_t* r, uint8_t* g, uint8_t* b, bool* muted);

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

static void Cmd_GET_SLOT(int argc, char **argv) {
    if (last_active_slot < 16) USB_Printf("LAST_SLOT:%d\n", last_active_slot);
    else USB_Printf("LAST_SLOT:NONE\n");
    if (fpga_is_ready) Apply_Slot_Clock(current_slot);
    else USB_Printf("LIVE_CLK:%d:FAILED\n", current_slot);
}

static void Cmd_START(int argc, char **argv) {
    if (argc < 4) return;
    if (!sd_is_mounted) { Error_Raise(ERR_SD_MOUNT_FAIL, SEV_WARNING, "Upload requires SD Card"); return; }

    int target_slot_int = atoi(argv[0]);
    char fname[30];
    bool valid_request = false;

    // Normal FPGA Bitstream Upload
    if (target_slot_int >= 1 && target_slot_int < 16) {
        snprintf(slot_names[target_slot_int], 16, "%s", argv[3]);
        slot_clk_configs[target_slot_int] = (uint8_t)atoi(argv[2]);
        Save_Configs();
        snprintf(fname, sizeof(fname), "0:/slot%d.bin", target_slot_int);
        valid_request = true;
    }
    // NEW: OTA Firmware Upload (Virtual Slot 99)
    else if (target_slot_int == 99) {
        snprintf(fname, sizeof(fname), "0:/ota.bin");
        valid_request = true;
    }

    if (valid_request) {
        if (f_open(&upload_fil, fname, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            current_upload_state = UPLOAD_RECEIVING;
            upload_bytes_total = strtoul(argv[1], NULL, 10);
            upload_bytes_received = 0; bytes_since_last_ack = 0; incoming_checksum = 0;
            last_rx_time = HAL_GetTick();

            while (osSemaphoreAcquire(BufferPoolSem, 0) == osOK);
            osSemaphoreRelease(BufferPoolSem); osSemaphoreRelease(BufferPoolSem);
            osSemaphoreAcquire(BufferPoolSem, osWaitForever);

            active_fill_buf = 0; sd_write_idx = 0;
            USB_Printf("ACK_START\n");
        } else {
            USB_Printf("ERR: Could not open file for writing\n");
        }
    }
}

static void Cmd_SETCLK(int argc, char **argv) {
    if (argc < 2) return;
    int s = atoi(argv[0]); int m = atoi(argv[1]);
    slot_clk_configs[s] = (uint8_t)m; Save_Configs();
    if (s == current_slot && fpga_is_ready) Apply_Slot_Clock(s);
    USB_Printf("LOG: Divider %d saved for Slot %d\n", m, s);
}

static void Cmd_SCAN(int argc, char **argv) {
    if (!sd_is_mounted) { Error_Raise(ERR_SD_MOUNT_FAIL, SEV_WARNING, "Scan requires SD Card"); return; }

    // CHANGE: Start loop at 0 instead of 1
    for (int i = 0; i < 16; i++) {
        if (slot_names[i][0] == '\0') USB_Printf("SLOT_NAME:%d:unconfigured\n", i);
        else USB_Printf("SLOT_NAME:%d:%s\n", i, slot_names[i]);
    }
    USB_Printf("LOG: SD Slot scan complete.\n");
}

static void Cmd_SD_TEST(int argc, char **argv) { if (sd_is_mounted) Diag_RunSDCardTest(); else Error_Raise(ERR_SD_MOUNT_FAIL, SEV_WARNING, "No SD Card"); }
static void Cmd_RAW_TEST(int argc, char **argv) { if (sd_is_mounted) Diag_RunRawSpeedTest(); else Error_Raise(ERR_SD_MOUNT_FAIL, SEV_WARNING, "No SD Card"); }
static void Cmd_SECTOR_TEST(int argc, char **argv) { if (sd_is_mounted) Diag_RunRawSectorTest(); else Error_Raise(ERR_SD_MOUNT_FAIL, SEV_WARNING, "No SD Card"); }
static void Cmd_READ_TEST(int argc, char **argv) { if (sd_is_mounted) Diag_RunUSBReadTest(); else Error_Raise(ERR_SD_MOUNT_FAIL, SEV_WARNING, "No SD Card"); }

static void Cmd_LED(int argc, char **argv) {
    if (argc < 2) return;
    if (strcmp(argv[0], "MUTE") == 0) {
        LED_SetMute((uint8_t)atoi(argv[1]));
        USB_Printf("LOG: LED Mute Applied\n");
    } else if (strcmp(argv[0], "PWM") == 0 && argc == 4) {
        LED_SetLimits(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        USB_Printf("LOG: Intensity Limits Updated.\n");
    } else if (strcmp(argv[0], "OVERRIDE") == 0) {
        LED_SetOverride(atoi(argv[1]) != 0);
        USB_Printf("LOG: LED Diagnostic Override Applied\n");
    }
}

static void Cmd_SPI(int argc, char **argv) {
    if (argc < 1) return;
    FpgaSpiPacket_t tx = {0};
    tx.sync = FPGA_SYNC_BYTE;

    if (strcmp(argv[0], "PING") == 0) { tx.cmd = 0x01; tx.length = 0; }
    else if (strcmp(argv[0], "ID") == 0)   { tx.cmd = 0x02; tx.length = 0; }

    // --- NEW GPIO COMMANDS ---
    else if (strcmp(argv[0], "WR") == 0 && argc == 3) {
        tx.cmd = strtol(argv[1], NULL, 16); // e.g. 0C
        tx.length = 1;
        tx.payload[0] = strtol(argv[2], NULL, 16);
    }
    else if (strcmp(argv[0], "GPIO_DIR") == 0 && argc == 2) {
        tx.cmd = 0x05; tx.length = 2;
        uint16_t val = strtol(argv[1], NULL, 16);
        tx.payload[0] = val >> 8; tx.payload[1] = val & 0xFF;
    }
    else if (strcmp(argv[0], "GPIO_WR") == 0 && argc == 2) {
        tx.cmd = 0x06; tx.length = 2;
        uint16_t val = strtol(argv[1], NULL, 16);
        tx.payload[0] = val >> 8; tx.payload[1] = val & 0xFF;
    }
    else if (strcmp(argv[0], "GPIO_RD") == 0) {
        tx.cmd = 0x07; tx.length = 0; // Return data will arrive in rx_packet.payload[0] and [1]
    }

    // --- NEW RAM / SPRAM COMMANDS ---
    else if (strcmp(argv[0], "RAM_WR") == 0 && argc == 3) {
        tx.cmd = 0x08; tx.length = 3;
        uint16_t addr = strtol(argv[1], NULL, 16);
        tx.payload[0] = addr >> 8; tx.payload[1] = addr & 0xFF;
        tx.payload[2] = strtol(argv[2], NULL, 16);
    }
    else if (strcmp(argv[0], "RAM_RD") == 0 && argc == 2) {
        tx.cmd = 0x09; tx.length = 2;
        uint16_t addr = strtol(argv[1], NULL, 16);
        tx.payload[0] = addr >> 8; tx.payload[1] = addr & 0xFF;
    }

    // --- NEW FIFO COMMANDS ---
    else if (strcmp(argv[0], "FIFO_WR") == 0 && argc == 2) {
        tx.cmd = 0x0A; tx.length = 1;
        tx.payload[0] = strtol(argv[1], NULL, 16);
    }
    else if (strcmp(argv[0], "FIFO_RD") == 0) {
        tx.cmd = 0x0B; tx.length = 0;
    }

    // Diagnostics routing
    else if (strcmp(argv[0], "BENCH") == 0) { tx.cmd = 0x99; tx.length = 0; }
    else if (strcmp(argv[0], "BULK") == 0) { tx.cmd = 0x9A; tx.length = 0; }

    if (tx.cmd != 0) {
        osMessageQueuePut(FpgaTxQueueHandle, &tx, 0, 0);
        USB_Printf("LOG: SPI Command 0x%02X Queued.\n", tx.cmd);
    } else {
        USB_Printf("ERR: Unknown SPI syntax. Check args.\n");
    }
}

static void Cmd_TELEM(int argc, char **argv) {
    if (argc < 2) return;
    if (strcmp(argv[0], "MUTE") == 0) {
        telem_is_muted = (uint8_t)atoi(argv[1]);
        USB_Printf("LOG: Telemetry stream %s.\n", telem_is_muted ? "MUTED" : "UNMUTED");
    } else if (strcmp(argv[0], "RATE") == 0) {
        int rate = atoi(argv[1]);

        // --- FIXED INDENTATION WARNING ---
        if (rate < 50) {
            rate = 50;
        }
        if (rate > 5000) {
            rate = 5000;
        }

        telem_rate_ms = rate;
        USB_Printf("LOG: Telemetry rate set to %d ms\n", rate);
    } else if (strcmp(argv[0], "FAST") == 0 && argc >= 3) {
        Telemetry_SetFastTarget(atoi(argv[1]), argv[2]);
        USB_Printf("LOG: Live Fast Mode Mapped.\n");
    }
}

// Helper function to copy files using the existing SD write buffer
static FRESULT Copy_Bitstream(const char* src, const char* dst) {
    FIL fsrc, fdst;
    FRESULT fr; UINT br, bw;

    fr = f_open(&fsrc, src, FA_READ);
    if (fr != FR_OK) return fr;

    fr = f_open(&fdst, dst, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) { f_close(&fsrc); return fr; }

    // We safely borrow sd_write_buffer[0] since we only do this when idle
    extern uint8_t sd_write_buffer[2][8192];
    while (f_read(&fsrc, sd_write_buffer[0], 8192, &br) == FR_OK && br > 0) {
        f_write(&fdst, sd_write_buffer[0], br, &bw);
        if (bw < br) break;
    }

    f_close(&fsrc);
    f_close(&fdst);
    return FR_OK;
}

static void Cmd_SYS(int argc, char **argv) {
    if (argc < 1) return;

    if (strcmp(argv[0], "RESET") == 0) {
        USB_Printf("LOG: Performing Software Reset...\n"); osDelay(100); NVIC_SystemReset();
    }
    else if (strcmp(argv[0], "SLEEP") == 0 && argc == 2) {
        if (atoi(argv[1])) SysPower_EnterSleep();
        else SysPower_Wake();
    }
    else if (strcmp(argv[0], "BANK") == 0) {
        uint32_t optr = FLASH->OPTR;
        // On STM32G0, if nSWAP_BANK is 1, Bank 1 is mapped to 0x08000000.
        uint8_t bank = READ_BIT(optr, FLASH_OPTR_nSWAP_BANK) ? 1 : 2;
        USB_Printf("OTA_LOG: [SYS] Active Flash Bank: %d (OPTR: 0x%08X)\n", bank, optr);
    }
    // --- NEW: Copy selected slot to Slot 0 ---
    else if (strcmp(argv[0], "SETZERO") == 0 && argc == 2) {
        int src_slot = atoi(argv[1]);
        if (src_slot >= 1 && src_slot <= 15) {
            char src_path[20];
            snprintf(src_path, sizeof(src_path), "0:/slot%d.bin", src_slot);

            USB_Printf("LOG: Copying Slot %d to default auto-boot Slot 0...\n", src_slot);
            if (Copy_Bitstream(src_path, "0:/slot0.bin") == FR_OK) {
                // Copy the metadata
            	strncpy(slot_names[0], slot_names[src_slot], 16);
            	slot_clk_configs[0] = slot_clk_configs[src_slot];
                Save_Configs();
                USB_Printf("LOG: Default bitstream updated successfully.\n");
            } else {
                USB_Printf("ERR: Failed to copy bitstream. Check SD card.\n");
            }
        }
    }
    // --- NEW: Erase all bitstreams except Slot 0 ---
    else if (strcmp(argv[0], "WIPE_SD") == 0) {
        USB_Printf("LOG: Wiping memory slots 1-15...\n");
        for (int i = 1; i < 16; i++) {
            char path[20];
            snprintf(path, sizeof(path), "0:/slot%d.bin", i);
            f_unlink(path); // Delete the file using FatFs
            slot_names[i][0] = '\0';
            slot_clk_configs[i] = 2; // Default prescaler
        }
        Save_Configs();
        USB_Printf("LOG: SD Wipe Complete. Slot 0 remains intact.\n");
    }
    else if (strcmp(argv[0], "OTA") == 0 && argc == 2) {
            char ota_path[32];
            snprintf(ota_path, sizeof(ota_path), "0:/%s", argv[1]);

            // This function blocks, flashes, and reboots the MCU.
            OTA_Update_From_SD(ota_path);
        }
    // --- NEW: Firmware Metadata Management ---
	else if (strcmp(argv[0], "SET_FW_INFO") == 0 && argc == 3) {
		FIL f; UINT bw;
		if (f_open(&f, "0:/fw_info.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
			char buf[64];
			snprintf(buf, sizeof(buf), "%s:%s\n", argv[1], argv[2]);
			f_write(&f, buf, strlen(buf), &bw);
			f_close(&f);
			USB_Printf("LOG: Firmware metadata staged.\n");
		} else {
			USB_Printf("ERR: Failed to save firmware metadata.\n");
		}
	}
	else if (strcmp(argv[0], "GET_FW_INFO") == 0) {
		FIL f; UINT br;
		char buf[64] = {0};
		if (f_open(&f, "0:/fw_info.txt", FA_READ) == FR_OK) {
			f_read(&f, buf, sizeof(buf)-1, &br);
			f_close(&f);
			// Clean newlines from the read buffer
			for(int i=0; i<64; i++) { if(buf[i]=='\n' || buf[i]=='\r') buf[i]='\0'; }
			USB_Printf("FW_INFO:%s\n", buf);
		} else {
			USB_Printf("FW_INFO:InkLab OS:v1.0\n"); // Default fallback
		}
	}
	else if (strcmp(argv[0], "I2C_BENCH") == 0) {
			Diag_RunI2CBenchmark();
		}
    else if (strcmp(argv[0], "FPGA_PWR") == 0 && argc == 2) {
        uint8_t state = atoi(argv[1]);
        if (state == 0) {
            FPGA_PowerDown(); // Call function in fpga.c
            fpga_is_ready = 0;
            USB_Printf("LOG: FPGA Power Down (RST=0, MCO=OFF)\n");
            USB_Printf("LIVE_CLK:%d:STOPPED\n", current_slot);
        } else {
            // Trigger the FPGA Task to re-program the current slot
            osThreadFlagsSet(FpgaTaskHandle, 0x01);
            USB_Printf("LOG: FPGA Power Up sequence started...\n");
        }
    }
}

static void Cmd_IO(int argc, char **argv) {
    if (argc < 1) return;
    if (strcmp(argv[0], "STATUS") == 0) {
        JoystickMap_t map = Joystick_GetMap();

        // Use default placeholders if you haven't added the LED getter to led_manager.c yet
        uint8_t r=50, g=50, b=50; bool m=false;
        LED_GetStatus(&r, &g, &b, &m);

        USB_Printf("{\"type\":\"io_stat\",\"r\":%d,\"g\":%d,\"b\":%d,\"m\":%d,\"ud\":%d,\"lr\":%d,\"cen\":%d}\n",
                   r, g, b, m, map.ud, map.lr, map.cen);
    } else if (strcmp(argv[0], "MAP") == 0 && argc == 4) {
        Joystick_SetMap(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        USB_Printf("LOG: Joystick Mapping Updated.\n");
    }
}

static void Cmd_BQ(int argc, char **argv) {
    if (argc < 1) return;
    const char* subcmd = argv[0];
    int val = (argc >= 2) ? atoi(argv[1]) : 0;

    if (strcmp(subcmd, "STATUS") == 0) {
        char dump_buf[512];
        if (BQ25798_DumpRegisters(dump_buf, sizeof(dump_buf)) == HAL_OK) USB_Printf("%s\n", dump_buf);
        else USB_Printf("ERR: BQ25798 I2C Read Failed\n");
        return;
    }

    if (strcmp(subcmd, "CHG") == 0) BQ25798_SetChargeEnable(val);
    else if (strcmp(subcmd, "HIZ") == 0) BQ25798_SetHiZ(val);
    else if (strcmp(subcmd, "OTG") == 0) BQ25798_SetOTG(val+50);
    else if (strcmp(subcmd, "PFM_FWD") == 0) BQ25798_SetPFM_FWD(val);
    else if (strcmp(subcmd, "PFM_OTG") == 0) BQ25798_SetPFM_OTG(val);
    else if (strcmp(subcmd, "WD") == 0) BQ25798_SetWatchdog(val);
    else if (strcmp(subcmd, "DETECT") == 0) BQ25798_ForceDetection();
    else if (strcmp(subcmd, "ICHG") == 0) BQ25798_SetChargeCurrent(val);
    else if (strcmp(subcmd, "ITERM") == 0) BQ25798_SetTermCurrent(val);
    else if (strcmp(subcmd, "IIN") == 0) BQ25798_SetInputCurrent(val);
    else if (strcmp(subcmd, "IOTG") == 0) BQ25798_SetOTGCurrent(val);
    else if (strcmp(subcmd, "VINDPM") == 0) BQ25798_SetInputVoltageLimit(val);
    else if (strcmp(subcmd, "VREG") == 0) BQ25798_SetChargeVoltage(val);
    else if (strcmp(subcmd, "VOTG") == 0) BQ25798_SetOTGVoltage(val);
    else if (strcmp(subcmd, "VSYS") == 0) BQ25798_SetMinSystemVoltage(val);
    else if (strcmp(subcmd, "BACKUP") == 0) BQ25798_SetBackup(val);
    else if (strcmp(subcmd, "DIS_IN") == 0) BQ25798_SetInputDisconnect(val);
    else if (strcmp(subcmd, "SHIP") == 0) BQ25798_SetShipMode(val);
    else if (strcmp(subcmd, "VBUS_OUT") == 0) BQ25798_SetBackupACFET(val);
    else if (strcmp(subcmd, "AUTO_REARM") == 0) BQ25798_SetAutoRearm(val);
    else if (strcmp(subcmd, "EMA") == 0) BQ25798_SetEMA(atof(argv[1]));
    else if (strcmp(subcmd, "TEST_BACKUP") == 0) {
        if (val == 1) {
            BQ25798_SetHiZ(0);             // 1. Ensure HIZ is off
            BQ25798_SetBackup(1);          // 2. Arm Backup Mode
            BQ25798_SetAutoRearm(0);       // 3. Disable Auto-Rearm (stops it fighting us)
            BQ25798_SetInputDisconnect(1); // 4. Simulate cord yank
            USB_Printf("LOG:[SIMULATION] Power Loss Triggered. Backup Active.\n");
        } else {
            BQ25798_SetAutoRearm(1);       // 1. Re-enable Auto-Rearm
            BQ25798_SetInputDisconnect(0); // 2. Drop the input disconnect so it can sense VBUS again!
            USB_Printf("LOG: [SIMULATION] Power Restored. Handing off to hardware...\n");
        }
        return;
    }
    else { USB_Printf("ERR: Unknown BQ command.\n"); return; }

    USB_Printf("LOG: BQ Setting [%s] Updated\n", subcmd);
}

static void Cmd_MEM(int argc, char **argv) {
    extern osThreadId_t UsbTaskHandle, FpgaTaskHandle, TelemetryTaskHandle, JoystickTaskHandle;

    // Returns remaining space in "Words" (4 bytes each on STM32)
    uint32_t usbMem  = uxTaskGetStackHighWaterMark(UsbTaskHandle);
    uint32_t fpgaMem = uxTaskGetStackHighWaterMark(FpgaTaskHandle);
    uint32_t telMem  = uxTaskGetStackHighWaterMark(TelemetryTaskHandle);
    uint32_t joyMem  = uxTaskGetStackHighWaterMark(JoystickTaskHandle);

    USB_Printf("--- STACK HIGH WATER MARKS (Words Remaining) ---\n");
    USB_Printf("USB Task:   %lu\n", usbMem);
    USB_Printf("FPGA Task:  %lu\n", fpgaMem);
    USB_Printf("Telem Task: %lu\n", telMem);
    USB_Printf("Joy Task:   %lu\n", joyMem);
    USB_Printf("----------------------------------------------\n");
}

static void Cmd_UART(int argc, char **argv) {
    if (argc < 1) return;
    extern UART_HandleTypeDef huart3;
    if (strcmp(argv[0], "TX") == 0 && argc == 2) {
        HAL_UART_Transmit(&huart3, (uint8_t*)argv[1], strlen(argv[1]), 100);
        USB_Printf("LOG: MCU transmitted UART '%s' (PB2 -> CON_15)\n", argv[1]);
    }
}

static void Cmd_ADC(int argc, char **argv) {
    if (argc < 1) return;
    extern ADC_HandleTypeDef hadc1;
    if (strcmp(argv[0], "READ") == 0) {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
            uint32_t raw_val = HAL_ADC_GetValue(&hadc1);
            float voltage = ((float)raw_val / 4095.0f) * 3.3f; // 12-bit ADC
            USB_Printf("{\"type\":\"adc_res\",\"v\":%.3f}\n", voltage);
        }
        HAL_ADC_Stop(&hadc1);
    }
}

static void Cmd_PINMUX(int argc, char **argv) {
    if (argc < 2) return;

    // Example: PINMUX:SET:PA5:USART3_TX
    if (strcmp(argv[0], "SET") == 0 && argc == 3) {
        Pinmux_SetPin(argv[1], argv[2]);
        // We don't print "Success" here to prevent terminal spam when applying all pins
    }
    // Example: PINMUX:APPLY (Fired after all SET commands are sent)
    else if (strcmp(argv[0], "APPLY") == 0) {
        USB_Printf("LOG: Hardware Multiplexer Routing Applied.\n");
        // Pinmux_SaveConfig(); // Implementation of saving to SD can go here
    }
}

static void Cmd_EXT(int argc, char **argv) {
    if (argc < 2) return;

    if (strcmp(argv[0], "UART") == 0 && argc == 3) {
        EXT_UART_Tx(atoi(argv[1]), argv[2]);
    }
    else if (strcmp(argv[0], "I2C") == 0 && argc == 4) {
        EXT_I2C_Write(atoi(argv[1]), strtol(argv[2], NULL, 16), strtol(argv[3], NULL, 16));
    }
    else if (strcmp(argv[0], "GPIO") == 0) {
        // FIX: Toggle has 4 arguments (GPIO, PIN, "TOGGLE", FREQ)
        if (argc == 4 && strcmp(argv[2], "TOGGLE") == 0) {
             EXT_GPIO_Toggle(argv[1], atoi(argv[3]));
        }
        // Write has 3 arguments (GPIO, PIN, STATE)
        else if (argc == 3) {
             EXT_GPIO_Write(argv[1], atoi(argv[2]));
        }
        else {
             USB_Printf("ERR: Invalid EXT:GPIO syntax.\n");
        }
    }
    else if (strcmp(argv[0], "ADC") == 0 && argc == 2) {
        EXT_ADC_Read(argv[1]);
    }
    else if (strcmp(argv[0], "DAC") == 0) {
		if (argc == 2 && strcmp(argv[1], "OFF") == 0) {
			EXT_DAC_Stop();
			USB_Printf("LOG: DAC Output disabled.\n");
		}
		else if (argc == 2) {
			EXT_DAC_Set(atof(argv[1]));
			USB_Printf("LOG: DAC DC Output set.\n");
		}
		else if (argc == 7 && strcmp(argv[1], "WAVE") == 0) {
			int wave = 0;
			if (strcmp(argv[2], "SINE") == 0) wave = 1;
			else if (strcmp(argv[2], "TRIANGLE") == 0) wave = 2;
			else if (strcmp(argv[2], "SQUARE") == 0) wave = 3;

			extern void EXT_DAC_Wave(uint8_t waveform, uint32_t freq_Hz, float offset_V, float amplitude_V, float vref_V);
			uint32_t freq = (uint32_t)strtoul(argv[3], NULL, 10);
			float offset = atof(argv[4]);
			float amp = atof(argv[5]);
			float vref = atof(argv[6]);

			EXT_DAC_Wave(wave, freq, offset, amp, vref);
			USB_Printf("LOG: DAC DMA Waveform Configured.\n");
		}
	}
	else if (strcmp(argv[0], "AD9837") == 0) {
		if (argc == 2 && strcmp(argv[1], "OFF") == 0) {
			EXT_AD9837_Stop(); // <--- New Hardware Stop
			USB_Printf("LOG: AD9837 disabled.\n");
		} else if (argc == 3) {
			int wave = 0; // 0 = SINE
			if (strcmp(argv[1], "TRIANGLE") == 0) wave = 1;
			else if (strcmp(argv[1], "SQUARE") == 0) wave = 2;

			EXT_AD9837_Set(wave, (uint32_t)strtoul(argv[2], NULL, 10));
			USB_Printf("LOG: AD9837 Configured via SPI.\n");
		}
	}
    else if (strcmp(argv[0], "EXT_DAC") == 0 && argc == 2) {
        EXT_DAC081_Set(atof(argv[1]));
    }
    else if (strcmp(argv[0], "SCAN") == 0 && argc == 2) {
        // Usage: EXT:SCAN:1  or  EXT:SCAN:2
        EXT_I2C_Scanner(atoi(argv[1]));
    }
    else {
        USB_Printf("ERR: Unknown EXT interface command.\n");
    }
}

static void Cmd_BENCHMARK(int argc, char **argv) {
    if (argc < 1) return;

    if (strcmp(argv[0], "NATIVE") == 0) {
        extern ADC_HandleTypeDef hadc1;
        extern UART_HandleTypeDef huart3;
        extern osMessageQueueId_t FpgaTxQueueHandle;

        uint32_t iterations = 10;
        USB_Printf("LOG: Starting Mixed-Signal Native Benchmark...\n");

        uint32_t start_time = HAL_GetTick(); // Get start time in ms

        for(uint32_t i = 0; i < iterations; i++) {
            // 1. SPI PING (Push to FPGA Task Queue)
            FpgaSpiPacket_t tx = {0};
            tx.sync = FPGA_SYNC_BYTE;
            tx.cmd = 0x01;
            tx.length = 0;
            osMessageQueuePut(FpgaTxQueueHandle, &tx, 0, 0);

            // 2. ADC READ (Block for conversion)
            HAL_ADC_Start(&hadc1);
            HAL_ADC_PollForConversion(&hadc1, 10);
            uint32_t raw_val = HAL_ADC_GetValue(&hadc1);
            HAL_ADC_Stop(&hadc1);
            (void)raw_val; // Prevent unused variable warning

            // 3. UART TX (Transmit 1 byte)
            HAL_UART_Transmit(&huart3, (uint8_t*)"B", 1, 10);

            // 4. GPIO TOGGLE (High then Low)
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
        }

        uint32_t end_time = HAL_GetTick();
        uint32_t duration = end_time - start_time;

        // Return exactly how long the MCU took
        USB_Printf("BENCH_RES:MCU:%lu\n", duration);
    }
    // JS End Marker
    else if (strcmp(argv[0], "JS_END") == 0) {
        USB_Printf("BENCH_RES:JS_END:0\n");
    }
}
static void Cmd_DELAY(int argc, char **argv) {
    if (argc == 1) {
        osDelay(atoi(argv[0])); // Allow scripts to pause execution
    }
}

static void Cmd_MACRO(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[0], "RUN") == 0) {
        char path[32];
        snprintf(path, sizeof(path), "0:/%s", argv[1]);

        FIL file;
        if (f_open(&file, path, FA_READ) == FR_OK) {
            char line[128];
            uint32_t start_time = HAL_GetTick();
            USB_Printf("LOG: Executing SD Macro '%s'...\n", argv[1]);

            while (f_gets(line, sizeof(line), &file)) {
                // Strip newlines and carriage returns
                line[strcspn(line, "\r\n")] = 0;

                // Skip empty lines or comments starting with #
                if (strlen(line) == 0 || line[0] == '#') continue;

                Frontend_ExecuteCommand(line);
            }

            uint32_t end_time = HAL_GetTick();
            f_close(&file);
            USB_Printf("BENCH_RES:MACRO:%lu\n", end_time - start_time);
        } else {
            USB_Printf("ERR: Macro file '%s' not found on SD.\n", argv[1]);
        }
    }
}



static const CommandDef_t command_table[] = {
    {"START",       Cmd_START},
    {"SETCLK",      Cmd_SETCLK},
    {"SCAN",        Cmd_SCAN},
    {"SD_TEST",     Cmd_SD_TEST},
    {"RAW_TEST",    Cmd_RAW_TEST},
    {"SECTOR_TEST", Cmd_SECTOR_TEST},
    {"READ_TEST",   Cmd_READ_TEST},
    {"LED",         Cmd_LED},
    {"SPI",         Cmd_SPI},
    {"TELEM",       Cmd_TELEM},
    {"SYS",         Cmd_SYS},
    {"BQ",          Cmd_BQ},
    {"IO",          Cmd_IO},
    {"GET_SLOT",    Cmd_GET_SLOT},
    {"MEM", Cmd_MEM},
    {"ADC",         Cmd_ADC},
    {"UART",        Cmd_UART},
    {"PINMUX",      Cmd_PINMUX},
    {"EXT",         Cmd_EXT},
    {"BENCHMARK",   Cmd_BENCHMARK},
    {"DELAY",       Cmd_DELAY},
    {"MACRO",       Cmd_MACRO},
    {NULL, NULL}
};

// ============================================================================
// RTOS INITIALIZATION & TASKS
// ============================================================================

void Frontend_DiskWriteTask(void *argument) {
    DiskWriteReq_t req;
    for(;;) {
        if (osMessageQueueGet(DiskWriteQueue, &req, NULL, osWaitForever) == osOK) {
            UINT bw;
            if (sd_is_mounted) f_write(&upload_fil, sd_write_buffer[req.buffer_idx], req.length, &bw);
            osSemaphoreRelease(BufferPoolSem);
        }
    }
}

void Frontend_Init(void) {
    current_upload_state = UPLOAD_IDLE;
    if (DiskWriteQueue == NULL) {
        DiskWriteQueue = osMessageQueueNew(2, sizeof(DiskWriteReq_t), NULL);
        BufferPoolSem = osSemaphoreNew(2, 2, NULL);
        osThreadNew(Frontend_DiskWriteTask, NULL, &(osThreadAttr_t){ .name="DiskTask", .priority=osPriorityAboveNormal, .stack_size=4096 });
    }
}

// ============================================================================
// CORE LOGIC: USB DATA ROUTER
// ============================================================================


// NEW: Universal command executor
void Frontend_ExecuteCommand(char* cmd_str) {
    char *tokens[10];
    int token_count = 0;
    char *token = cmd_str;

    while (*token != '\0' && token_count < 10) {
        tokens[token_count++] = token;
        char *next_colon = strchr(token, ':');
        if (next_colon) { *next_colon = '\0'; token = next_colon + 1; }
        else break;
    }

    if (token_count > 0) {
        bool found = false;
        for (int j = 0; command_table[j].cmd != NULL; j++) {
            if (strcmp(tokens[0], command_table[j].cmd) == 0) {
                command_table[j].handler(token_count - 1, &tokens[1]);
                found = true; break;
            }
        }
        if (!found) USB_Printf("ERR: Unknown command '%s'\n", tokens[0]);
    }
}


void Frontend_ProcessBlock(const uint8_t* data, uint16_t len) {
    uint16_t i = 0;

    while (i < len) {
        if (current_upload_state == UPLOAD_IDLE) {

            // FIX: Only increment once per byte!
            uint8_t c = data[i++];

            if (c == '\n' || c == '\r') {
                if (cmd_idx == 0) continue;
                cmd_buffer[cmd_idx] = '\0';

                // Use our new universal executor!
                Frontend_ExecuteCommand(cmd_buffer);

                cmd_idx = 0;
            }
            else if (cmd_idx < sizeof(cmd_buffer) - 1) {
                cmd_buffer[cmd_idx++] = c;
            }
        }
        else {
            // Upload State Logic (Leave this exactly as it was)
            last_rx_time = HAL_GetTick();
            uint32_t max_copy = (8192 - sd_write_idx < upload_bytes_total - upload_bytes_received) ? 8192 - sd_write_idx : upload_bytes_total - upload_bytes_received;
            uint32_t process_len = (len - i < max_copy) ? len - i : max_copy;

            for(uint32_t k = 0; k < process_len; k++) incoming_checksum ^= data[i + k];
            memcpy(&sd_write_buffer[active_fill_buf][sd_write_idx], &data[i], process_len);

            sd_write_idx += process_len; upload_bytes_received += process_len; bytes_since_last_ack += process_len; i += process_len;

            if (sd_write_idx >= 8192 || upload_bytes_received == upload_bytes_total) {
                DiskWriteReq_t req = {active_fill_buf, sd_write_idx};
                osMessageQueuePut(DiskWriteQueue, &req, 0, osWaitForever);
                if (upload_bytes_received < upload_bytes_total) {
                    active_fill_buf = (active_fill_buf + 1) % 2; sd_write_idx = 0;
                    osSemaphoreAcquire(BufferPoolSem, osWaitForever);
                }
            }

            if (bytes_since_last_ack >= 8192 && upload_bytes_received < upload_bytes_total) {
                USB_Printf("ACK_CHUNK\n"); bytes_since_last_ack -= 8192;
            }

            if (upload_bytes_received >= upload_bytes_total) {
                osSemaphoreAcquire(BufferPoolSem, osWaitForever); osSemaphoreAcquire(BufferPoolSem, osWaitForever);
                f_close(&upload_fil); current_upload_state = UPLOAD_IDLE;
                USB_Printf("ACK_DONE\n"); USB_Printf("CSUM: 0x%02X\n", incoming_checksum);
                osSemaphoreRelease(BufferPoolSem); osSemaphoreRelease(BufferPoolSem);
            }
        }
    }
}

void Frontend_CheckTimeout(void) {
    if (current_upload_state == UPLOAD_RECEIVING && (HAL_GetTick() - last_rx_time) > 3000) {
        f_close(&upload_fil); current_upload_state = UPLOAD_IDLE; cmd_idx = 0;
        USB_Printf("ERR_TIMEOUT\n");
        while (osSemaphoreAcquire(BufferPoolSem, 0) == osOK);
        osSemaphoreRelease(BufferPoolSem); osSemaphoreRelease(BufferPoolSem);
    }
}
