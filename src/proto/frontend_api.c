// ============================================================================
// INCLUDES
// ============================================================================
#include "proto/frontend_api.h"
#include "system_config.h"

// Zephyr kernel & subsystems
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/fs/fs.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/debug/thread_analyzer.h>

// Application HAL modules
#include "hal/bq25798.h"
#include "hal/fpga.h"
#include "hal/led_manager.h"
#include "hal/joystick.h"
#include "hal/diagnostics.h"
#include "hal/sys_power.h"
#include "hal/ext_interfaces.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// PRIVATE TYPES
// ============================================================================

typedef struct {
    uint8_t buffer_idx;
    uint16_t length;
} DiskWriteReq_t;

typedef enum {
    UPLOAD_IDLE,
    UPLOAD_RECEIVING
} UploadState_t;

typedef void (*CommandHandler_t)(int argc, char **argv);

typedef struct {
    const char        *cmd;
    CommandHandler_t   handler;
} CommandDef_t;

// ============================================================================
// ZEPHYR RTOS PRIMITIVES
// ============================================================================

/* Message queue: 2 pending write requests, 4-byte aligned */
K_MSGQ_DEFINE(disk_write_queue, sizeof(DiskWriteReq_t), SD_WRITE_QUEUE_SIZE, SD_WRITE_QUEUE_ALIGN);

/* Semaphore: dual-buffer pool (initial=2, max=2) */
K_SEM_DEFINE(buffer_pool_sem, 2, 2);

/* Disk-write task forward declaration */
static void frontend_disk_write_task(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(disk_write_tid,
                DISK_TASK_STACK,
                frontend_disk_write_task,
                NULL, NULL, NULL,
                DISK_TASK_PRIO, 0, 0); 

// ============================================================================
// MODULE STATE
// ============================================================================

/* Dual write buffers — also borrowed by Diagnostics when upload is IDLE */
__attribute__((aligned(4))) uint8_t sd_write_buffer[2][SD_BUF_SIZE];

static uint8_t      active_fill_buf         = 0;
static uint32_t     sd_write_idx            = 0;

static UploadState_t current_upload_state   = UPLOAD_IDLE;
static struct fs_file_t upload_fil;
static uint32_t     upload_bytes_total      = 0;
static uint32_t     upload_bytes_received   = 0;
static uint32_t     bytes_since_last_ack    = 0;
static uint8_t      incoming_checksum       = 0;

static char         cmd_buffer[CMD_BUF_LEN];
static uint16_t     cmd_idx                 = 0;
static uint32_t     last_rx_time            = 0;

// ============================================================================
// EXTERNAL STATE (declared in other modules — keep minimal)
//
// NOTE: If any of these produce linker errors it means the owning module
// has not been ported yet. Provide a stub .c file that defines the symbol
// until the full port is complete.
// ============================================================================
extern volatile uint8_t  sd_is_mounted;        /* app_fatfs.c  */
extern volatile uint8_t  telem_is_muted;       /* telemetry.c  */
extern volatile uint32_t telem_rate_ms;        /* telemetry.c  */

extern uint8_t  current_slot;                  /* fpga.c       */
extern uint8_t  fpga_is_ready;                 /* fpga.c       */
extern char     slot_names[FPGA_MAX_SLOTS][FPGA_SLOT_NAME_LEN];            /* fpga.c       */
extern uint8_t  slot_clk_configs[FPGA_MAX_SLOTS];          /* fpga.c       */
extern uint8_t  last_active_slot;              /* fpga.c       */

/* Config persistence helpers (implement in a config.c module) */
extern void Save_Configs(void);
extern void Apply_Slot_Clock(uint8_t slot);

/* Telemetry fast-mode helper */
extern void Telemetry_SetFastTarget(uint8_t ch, const char *target);

/* OTA — keep as HAL shim until MCUboot port is complete */
extern int OTA_Update_From_SD(const char *filename);

// ============================================================================
// FORWARD DECLARATIONS (command handlers defined below)
// ============================================================================
static void Cmd_START(int argc, char **argv);
static void Cmd_SETCLK(int argc, char **argv);
static void Cmd_SCAN(int argc, char **argv);
static void Cmd_SD_TEST(int argc, char **argv);
static void Cmd_RAW_TEST(int argc, char **argv);
static void Cmd_SECTOR_TEST(int argc, char **argv);
static void Cmd_READ_TEST(int argc, char **argv);
static void Cmd_LED(int argc, char **argv);
static void Cmd_SPI(int argc, char **argv);
static void Cmd_TELEM(int argc, char **argv);
static void Cmd_SYS(int argc, char **argv);
static void Cmd_BQ(int argc, char **argv);
static void Cmd_IO(int argc, char **argv);
static void Cmd_GET_SLOT(int argc, char **argv);
static void Cmd_MEM(int argc, char **argv);
static void Cmd_ADC(int argc, char **argv);
static void Cmd_UART(int argc, char **argv);
static void Cmd_EXT(int argc, char **argv);
static void Cmd_BENCHMARK(int argc, char **argv);
static void Cmd_DELAY(int argc, char **argv);
static void Cmd_MACRO(int argc, char **argv);

void Frontend_ExecuteCommand(char *cmd_str); /* public, used by Cmd_MACRO */

// ============================================================================
// COMMAND TABLE
// ============================================================================
static const CommandDef_t command_table[] = {
    { "START",        Cmd_START        },
    { "SETCLK",       Cmd_SETCLK       },
    { "SCAN",         Cmd_SCAN         },
    { "SD_TEST",      Cmd_SD_TEST      },
    { "RAW_TEST",     Cmd_RAW_TEST     },
    { "SECTOR_TEST",  Cmd_SECTOR_TEST  },
    { "READ_TEST",    Cmd_READ_TEST    },
    { "LED",          Cmd_LED          },
    { "SPI",          Cmd_SPI          },
    { "TELEM",        Cmd_TELEM        },
    { "SYS",          Cmd_SYS          },
    { "BQ",           Cmd_BQ           },
    { "IO",           Cmd_IO           },
    { "GET_SLOT",     Cmd_GET_SLOT     },
    { "MEM",          Cmd_MEM          },
    { "ADC",          Cmd_ADC          },
    { "UART",         Cmd_UART         },
    { "EXT",          Cmd_EXT          },
    { "BENCHMARK",    Cmd_BENCHMARK    },
    { "DELAY",        Cmd_DELAY        },
    { "MACRO",        Cmd_MACRO        },
    { NULL,           NULL             }
};

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * @brief Drain all tokens from the semaphore pool back to maximum count.
 *
 * Used to reset the buffer-pool semaphore before starting a new upload without
 * calling an OS-specific "reset" (which Zephyr does not provide).
 */
static void buffer_pool_reset(void)
{
    /* Take all available tokens (non-blocking) to reach count = 0 */
    while (k_sem_take(&buffer_pool_sem, K_NO_WAIT) == 0) { /* drain */ }
    /* Restore both tokens */
    k_sem_give(&buffer_pool_sem);
    k_sem_give(&buffer_pool_sem);
}

/**
 * @brief Build a full SD path from a bare filename.
 *        e.g. "slot1.bin" -> "/SD:/slot1.bin"
 */
static void sd_path(char *out, size_t out_size, const char *filename)
{
    snprintf(out, out_size, "%s/%s", SD_MOUNT_POINT, filename);
}

/**
 * @brief Copy a bitstream file on the SD card.
 *
 * Borrows sd_write_buffer[0] as a 8 KiB staging buffer.
 * Must only be called when current_upload_state == UPLOAD_IDLE.
 */
static int copy_bitstream(const char *src_path, const char *dst_path)
{
    struct fs_file_t src, dst;
    fs_file_t_init(&src);
    fs_file_t_init(&dst);

    int rc = fs_open(&src, src_path, FS_O_READ);
    if (rc < 0) { return rc; }

    rc = fs_open(&dst, dst_path, FS_O_WRITE | FS_O_CREATE);
    if (rc < 0) { fs_close(&src); return rc; }

    ssize_t br;
    while ((br = fs_read(&src, sd_write_buffer[0], SD_BUF_SIZE)) > 0) {
        ssize_t bw = fs_write(&dst, sd_write_buffer[0], (size_t)br);
        if (bw < br) { rc = -EIO; break; }
    }

    fs_close(&src);
    fs_close(&dst);
    return rc;
}

// ============================================================================
// DISK-WRITE TASK
// ============================================================================

static void frontend_disk_write_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    DiskWriteReq_t req;
    while (1) {
        if (k_msgq_get(&disk_write_queue, &req, K_FOREVER) == 0) {
            if (sd_is_mounted) {
                fs_write(&upload_fil,
                         sd_write_buffer[req.buffer_idx],
                         req.length);
            }
            k_sem_give(&buffer_pool_sem);
        }
    }
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

/** @brief Queries and returns the active FPGA slot and running clock parameters. */
static void Cmd_GET_SLOT(int argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);

    if (last_active_slot < FPGA_MAX_SLOTS) {
        printk("LAST_SLOT:%d\n", last_active_slot);
    } else {
        printk("LAST_SLOT:NONE\n");
    }

    if (fpga_is_ready) {
        Apply_Slot_Clock(current_slot);
    } else {
        printk("LIVE_CLK:%d:FAILED\n", current_slot);
    }
}

// ----------------------------------------------------------------------------
/** @brief Initializes a file stream write for a new bitstream or OTA firmware chunk. */
static void Cmd_START(int argc, char **argv)
{
    if (argc < 4) { return; }
    if (!sd_is_mounted) {
        printk("ERR: Upload requires SD Card\n");
        return;
    }

    int target_slot = atoi(argv[0]);
    char fpath[SD_MAX_PATH_LEN];
    bool valid = false;

    if (target_slot >= 1 && target_slot < FPGA_MAX_SLOTS) {
        snprintf(slot_names[target_slot], 16, "%s", argv[3]);
        slot_clk_configs[target_slot] = (uint8_t)atoi(argv[2]);
        Save_Configs();
        sd_path(fpath, sizeof(fpath), "slot");
        snprintf(fpath, sizeof(fpath), "%s/slot%d.bin", SD_MOUNT_POINT, target_slot);
        valid = true;
    } else if (target_slot == SD_SLOT_OTA) {
        snprintf(fpath, sizeof(fpath), "%s/ota.bin", SD_MOUNT_POINT);
        valid = true;
    }

    if (!valid) {
        printk("ERR: Invalid slot number\n");
        return;
    }

    fs_file_t_init(&upload_fil);
    int rc = fs_open(&upload_fil, fpath, FS_O_WRITE | FS_O_CREATE);
    if (rc < 0) {
        printk("ERR: Could not open file for writing (%d)\n", rc);
        return;
    }

    current_upload_state  = UPLOAD_RECEIVING;
    upload_bytes_total    = (uint32_t)strtoul(argv[1], NULL, 10);
    upload_bytes_received = 0;
    bytes_since_last_ack  = 0;
    incoming_checksum     = 0;
    last_rx_time          = k_uptime_get_32();

    buffer_pool_reset();
    k_sem_take(&buffer_pool_sem, K_FOREVER); /* claim one buffer */

    active_fill_buf = 0;
    sd_write_idx    = 0;
    printk("ACK_START\n");
}

// ----------------------------------------------------------------------------
/** @brief Sets and saves the operating clock divider for a specific FPGA slot. */
static void Cmd_SETCLK(int argc, char **argv)
{
    if (argc < 2) { return; }
    int s = atoi(argv[0]);
    int m = atoi(argv[1]);
    slot_clk_configs[s] = (uint8_t)m;
    Save_Configs();
    if (s == current_slot && fpga_is_ready) {
        Apply_Slot_Clock(s);
    }
    printk("LOG: Divider %d saved for Slot %d\n", m, s);
}

// ----------------------------------------------------------------------------
/** @brief Iterates the SD card slot directory and returns the configured names. */
static void Cmd_SCAN(int argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);

    if (!sd_is_mounted) {
        printk("ERR: Scan requires SD Card\n");
        return;
    }
    for (int i = 0; i < FPGA_MAX_SLOTS; i++) {
        if (slot_names[i][0] == '\0') {
            printk("SLOT_NAME:%d:unconfigured\n", i);
        } else {
            printk("SLOT_NAME:%d:%s\n", i, slot_names[i]);
        }
    }
    printk("LOG: SD Slot scan complete.\n");
}

// ----------------------------------------------------------------------------
/** @brief Triggers the filesystem diagnostics test for read/write integrity. */
static void Cmd_SD_TEST(int argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    if (sd_is_mounted) { Diag_RunSDCardTest(); }
    else { printk("ERR: No SD Card\n"); }
}

/** @brief Command handler for RAW_TEST speed benchmark. */
static void Cmd_RAW_TEST(int argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    if (sd_is_mounted) { Diag_RunRawSpeedTest(); }
    else { printk("ERR: No SD Card\n"); }
}

/** @brief Command handler for SECTOR_TEST raw SPI benchmark. */
static void Cmd_SECTOR_TEST(int argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    if (sd_is_mounted) { Diag_RunRawSectorTest(); }
    else { printk("ERR: No SD Card\n"); }
}

/** @brief Command handler for READ_TEST USB uplink test. */
static void Cmd_READ_TEST(int argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    if (sd_is_mounted) { Diag_RunUSBReadTest(); }
    else { printk("ERR: No SD Card\n"); }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for controlling LED parameters. */
static void Cmd_LED(int argc, char **argv)
{
    if (argc < 2) { return; }

    if (strcmp(argv[0], "MUTE") == 0) {
        LED_SetMute((uint8_t)atoi(argv[1]));
        printk("LOG: LED Mute Applied\n");
    } else if (strcmp(argv[0], "PWM") == 0 && argc == 4) {
        LED_SetLimits(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        printk("LOG: Intensity Limits Updated.\n");
    } else if (strcmp(argv[0], "OVERRIDE") == 0) {
        LED_SetOverride(atoi(argv[1]) != 0);
        printk("LOG: LED Diagnostic Override Applied\n");
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for generic SPI operations to the FPGA. */
static void Cmd_SPI(int argc, char **argv)
{
    if (argc < 1) { return; }

    FpgaSpiPacket_t tx = {0};
    tx.sync = FPGA_SYNC_BYTE;

    if (strcmp(argv[0], "PING") == 0) {
        tx.cmd = 0x01; tx.length = 0;
    } else if (strcmp(argv[0], "ID") == 0) {
        tx.cmd = 0x02; tx.length = 0;
    } else if (strcmp(argv[0], "WR") == 0 && argc == 3) {
        tx.cmd        = (uint8_t)strtol(argv[1], NULL, 16);
        tx.length     = 1;
        tx.payload[0] = (uint8_t)strtol(argv[2], NULL, 16);
    } else if (strcmp(argv[0], "GPIO_DIR") == 0 && argc == 2) {
        uint16_t val  = (uint16_t)strtol(argv[1], NULL, 16);
        tx.cmd        = 0x05; tx.length = 2;
        tx.payload[0] = val >> 8; tx.payload[1] = val & 0xFF;
    } else if (strcmp(argv[0], "GPIO_WR") == 0 && argc == 2) {
        uint16_t val  = (uint16_t)strtol(argv[1], NULL, 16);
        tx.cmd        = 0x06; tx.length = 2;
        tx.payload[0] = val >> 8; tx.payload[1] = val & 0xFF;
    } else if (strcmp(argv[0], "GPIO_RD") == 0) {
        tx.cmd = 0x07; tx.length = 0;
    } else if (strcmp(argv[0], "RAM_WR") == 0 && argc == 3) {
        uint16_t addr = (uint16_t)strtol(argv[1], NULL, 16);
        tx.cmd        = 0x08; tx.length = 3;
        tx.payload[0] = addr >> 8; tx.payload[1] = addr & 0xFF;
        tx.payload[2] = (uint8_t)strtol(argv[2], NULL, 16);
    } else if (strcmp(argv[0], "RAM_RD") == 0 && argc == 2) {
        uint16_t addr = (uint16_t)strtol(argv[1], NULL, 16);
        tx.cmd        = 0x09; tx.length = 2;
        tx.payload[0] = addr >> 8; tx.payload[1] = addr & 0xFF;
    } else if (strcmp(argv[0], "FIFO_WR") == 0 && argc == 2) {
        tx.cmd        = 0x0A; tx.length = 1;
        tx.payload[0] = (uint8_t)strtol(argv[1], NULL, 16);
    } else if (strcmp(argv[0], "FIFO_RD") == 0) {
        tx.cmd = 0x0B; tx.length = 0;
    } else if (strcmp(argv[0], "BENCH") == 0) {
        tx.cmd = 0x99; tx.length = 0;
    } else if (strcmp(argv[0], "BULK") == 0) {
        tx.cmd = 0x9A; tx.length = 0;
    } else {
        printk("ERR: Unknown SPI syntax. Check args.\n");
        return;
    }

    if (FPGA_EnqueueSpiPacket(&tx) == 0) {
        printk("LOG: SPI Command 0x%02X Queued.\n", tx.cmd);
    } else {
        printk("ERR: FPGA TX queue full.\n");
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for Telemetry configuration and rate adjustment. */
static void Cmd_TELEM(int argc, char **argv)
{
    if (argc < 2) { return; }

    if (strcmp(argv[0], "MUTE") == 0) {
        telem_is_muted = (uint8_t)atoi(argv[1]);
        printk("LOG: Telemetry stream %s.\n",
               telem_is_muted ? "MUTED" : "UNMUTED");
    } else if (strcmp(argv[0], "RATE") == 0) {
        int rate = atoi(argv[1]);
        if (rate < 50)   { rate = 50;   }
        if (rate > 5000) { rate = 5000; }
        telem_rate_ms = (uint32_t)rate;
        printk("LOG: Telemetry rate set to %d ms\n", rate);
    } else if (strcmp(argv[0], "FAST") == 0 && argc >= 3) {
        Telemetry_SetFastTarget(atoi(argv[1]), argv[2]);
        printk("LOG: Live Fast Mode Mapped.\n");
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for System level commands (Sleep, OTA, FS Ops). */
static void Cmd_SYS(int argc, char **argv)
{
    if (argc < 1) { return; }

    if (strcmp(argv[0], "RESET") == 0) {
        printk("LOG: Performing Software Reset...\n");
        k_msleep(100);
        sys_reboot(SYS_REBOOT_COLD);

    } else if (strcmp(argv[0], "SLEEP") == 0 && argc == 2) {
        if (atoi(argv[1])) { SysPower_EnterSleep(); }
        else               { SysPower_Wake();        }

    } else if (strcmp(argv[0], "BANK") == 0) {
        /* NOTE [OTA]: STM32-specific FLASH option bytes.
         * Replace with mcuboot_swap_type() or board_get_active_bank() when
         * the MCUboot port is complete. Until then the shim below reads the
         * OPTR register via CMSIS if the STM32 HAL headers are available.   */
#if defined(FLASH) && defined(FLASH_OPTR_nSWAP_BANK)
        uint32_t optr = FLASH->OPTR;
        uint8_t  bank = (optr & FLASH_OPTR_nSWAP_BANK) ? 1 : 2;
        printk("OTA_LOG: [SYS] Active Flash Bank: %d (OPTR: 0x%08X)\n",
               bank, (unsigned int)optr);
#else
        printk("OTA_LOG: [SYS] Bank query not supported on this target.\n");
#endif

    } else if (strcmp(argv[0], "SETZERO") == 0 && argc == 2) {
        int src = atoi(argv[1]);
        if (src >= 1 && src <= 15) {
            char src_path[SD_MAX_PATH_LEN], dst_path[SD_MAX_PATH_LEN]; 
            snprintf(src_path, sizeof(src_path), "%s/slot%d.bin", SD_MOUNT_POINT, src);
            snprintf(dst_path, sizeof(dst_path), "%s/slot0.bin",  SD_MOUNT_POINT);
            printk("LOG: Copying Slot %d to default auto-boot Slot 0...\n", src);
            if (copy_bitstream(src_path, dst_path) == 0) {
                strncpy(slot_names[0], slot_names[src], FPGA_SLOT_NAME_LEN);
                slot_clk_configs[0] = slot_clk_configs[src];
                Save_Configs();
                printk("LOG: Default bitstream updated successfully.\n");
            } else {
                printk("ERR: Failed to copy bitstream. Check SD card.\n");
            }
        }

    } else if (strcmp(argv[0], "WIPE_SD") == 0) {
        printk("LOG: Wiping memory slots...\n");
        char path[SD_MAX_PATH_LEN];
        for (int i = 1; i < FPGA_MAX_SLOTS; i++) {
            snprintf(path, sizeof(path), "%s/slot%d.bin", SD_MOUNT_POINT, i);
            fs_unlink(path);
            slot_names[i][0]    = '\0';
            slot_clk_configs[i] = 2;
        }
        Save_Configs();
        printk("LOG: SD Wipe Complete. Slot 0 remains intact.\n");

    } else if (strcmp(argv[0], "OTA") == 0 && argc == 2) {
        char ota_path[SD_MAX_PATH_LEN];
        snprintf(ota_path, sizeof(ota_path), "%s/%s", SD_MOUNT_POINT, argv[1]);
        /* NOTE [OTA]: OTA_Update_From_SD is a HAL shim — replace with
         * MCUboot DFU flow when available.                               */
        OTA_Update_From_SD(ota_path);

    } else if (strcmp(argv[0], "SET_FW_INFO") == 0 && argc == 3) {
        struct fs_file_t f;
        char fw_path[SD_MAX_PATH_LEN];
        snprintf(fw_path, sizeof(fw_path), "%s/fw_info.txt", SD_MOUNT_POINT);
        fs_file_t_init(&f);
        if (fs_open(&f, fw_path, FS_O_WRITE | FS_O_CREATE) == 0) {
            char buf[64];
            int  len = snprintf(buf, sizeof(buf), "%s:%s\n", argv[1], argv[2]);
            fs_write(&f, buf, (size_t)len);
            fs_close(&f);
            printk("LOG: Firmware metadata staged.\n");
        } else {
            printk("ERR: Failed to save firmware metadata.\n");
        }

    } else if (strcmp(argv[0], "GET_FW_INFO") == 0) {
        struct fs_file_t f;
        char fw_path[SD_MAX_PATH_LEN];
        snprintf(fw_path, sizeof(fw_path), "%s/fw_info.txt", SD_MOUNT_POINT);
        fs_file_t_init(&f);
        if (fs_open(&f, fw_path, FS_O_READ) == 0) {
            char buf[64] = {0};
            fs_read(&f, buf, sizeof(buf) - 1);
            fs_close(&f);
            /* Strip CR/LF */
            for (int i = 0; i < 64; i++) {
                if (buf[i] == '\n' || buf[i] == '\r') { buf[i] = '\0'; }
            }
            printk("FW_INFO:%s\n", buf);
        } else {
            printk("FW_INFO:InkLab OS:v1.0\n");
        }

    } else if (strcmp(argv[0], "I2C_BENCH") == 0) {
        Diag_RunI2CBenchmark();

    } else if (strcmp(argv[0], "FPGA_PWR") == 0 && argc == 2) {
        if (atoi(argv[1]) == 0) {
            FPGA_PowerDown();
            fpga_is_ready = 0;
            printk("LOG: FPGA Power Down (RST=0, MCO=OFF)\n");
            printk("LIVE_CLK:%d:STOPPED\n", current_slot);
        } else {
            /* NOTE [FPGA]: Replace with your Zephyr FPGA task signal.
             * Options: k_event_post(), k_sem_give(), k_msgq_put() on a
             * dedicated control queue.                                   */
            FPGA_RequestPowerUp();
            printk("LOG: FPGA Power Up sequence started...\n");
        }

    } else {
        printk("ERR: Unknown SYS command '%s'\n", argv[0]);
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for BQ25798 charger configuration. */
static void Cmd_BQ(int argc, char **argv)
{
    if (argc < 1) { return; }
    const char *subcmd = argv[0];
    int val = (argc >= 2) ? atoi(argv[1]) : 0;

    if (strcmp(subcmd, "STATUS") == 0) {
        char dump_buf[512];
        /* NOTE: BQ25798_DumpRegisters returns 0 on success (not HAL_OK=0).
         * This is already correct — HAL_OK == 0 so the check is equivalent. */
        if (BQ25798_DumpRegisters(dump_buf, sizeof(dump_buf)) == 0) {
            printk("%s\n", dump_buf);
        } else {
            printk("ERR: BQ25798 I2C Read Failed\n");
        }
        return;
    }

    if      (strcmp(subcmd, "CHG")         == 0) BQ25798_SetChargeEnable(val);
    else if (strcmp(subcmd, "HIZ")         == 0) BQ25798_SetHiZ(val);
    else if (strcmp(subcmd, "OTG")         == 0) BQ25798_SetOTG(val + 50);
    else if (strcmp(subcmd, "PFM_FWD")     == 0) BQ25798_SetPFM_FWD(val);
    else if (strcmp(subcmd, "PFM_OTG")     == 0) BQ25798_SetPFM_OTG(val);
    else if (strcmp(subcmd, "WD")          == 0) BQ25798_SetWatchdog(val);
    else if (strcmp(subcmd, "DETECT")      == 0) BQ25798_ForceDetection();
    else if (strcmp(subcmd, "ICHG")        == 0) BQ25798_SetChargeCurrent((uint16_t)val);
    else if (strcmp(subcmd, "ITERM")       == 0) BQ25798_SetTermCurrent((uint16_t)val);
    else if (strcmp(subcmd, "IIN")         == 0) BQ25798_SetInputCurrent((uint16_t)val);
    else if (strcmp(subcmd, "IOTG")        == 0) BQ25798_SetOTGCurrent((uint16_t)val);
    else if (strcmp(subcmd, "VINDPM")      == 0) BQ25798_SetInputVoltageLimit((uint16_t)val);
    else if (strcmp(subcmd, "VREG")        == 0) BQ25798_SetChargeVoltage((uint16_t)val);
    else if (strcmp(subcmd, "VOTG")        == 0) BQ25798_SetOTGVoltage((uint16_t)val);
    else if (strcmp(subcmd, "VSYS")        == 0) BQ25798_SetMinSystemVoltage((uint16_t)val);
    else if (strcmp(subcmd, "BACKUP")      == 0) BQ25798_SetBackup((uint8_t)val);
    else if (strcmp(subcmd, "DIS_IN")      == 0) BQ25798_SetInputDisconnect((uint8_t)val);
    else if (strcmp(subcmd, "SHIP")        == 0) BQ25798_SetShipMode((uint8_t)val);
    else if (strcmp(subcmd, "VBUS_OUT")    == 0) BQ25798_SetBackupACFET((uint8_t)val);
    else if (strcmp(subcmd, "AUTO_REARM")  == 0) BQ25798_SetAutoRearm((uint8_t)val);
    else if (strcmp(subcmd, "EMA")         == 0 && argc >= 2) {
        BQ25798_SetEMA((float)atof(argv[1]));
    }
    else if (strcmp(subcmd, "TEST_BACKUP") == 0) {
        if (val == 1) {
            BQ25798_SetHiZ(0);
            BQ25798_SetBackup(1);
            BQ25798_SetAutoRearm(0);
            BQ25798_SetInputDisconnect(1);
            printk("LOG: [SIMULATION] Power Loss Triggered. Backup Active.\n");
        } else {
            BQ25798_SetAutoRearm(1);
            BQ25798_SetInputDisconnect(0);
            printk("LOG: [SIMULATION] Power Restored. Handing off to hardware...\n");
        }
        return;
    }
    else {
        printk("ERR: Unknown BQ command '%s'\n", subcmd);
        return;
    }

    printk("LOG: BQ Setting [%s] Updated\n", subcmd);
}

// ----------------------------------------------------------------------------
/** @brief Command handler for Joystick mapping and IO status. */
static void Cmd_IO(int argc, char **argv)
{
    if (argc < 1) { return; }

    if (strcmp(argv[0], "STATUS") == 0) {
        JoystickMap_t map = Joystick_GetMap();
        uint8_t r = 50, g = 50, b = 50;
        bool    m = false;
        LED_GetStatus(&r, &g, &b, &m);
        printk("{\"type\":\"io_stat\","
               "\"r\":%d,\"g\":%d,\"b\":%d,\"m\":%d,"
               "\"ud\":%d,\"lr\":%d,\"cen\":%d}\n",
               r, g, b, (int)m, map.ud, map.lr, map.cen);
    } else if (strcmp(argv[0], "MAP") == 0 && argc == 4) {
        Joystick_SetMap(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
        printk("LOG: Joystick Mapping Updated.\n");
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for RTOS Memory and Thread Stack analysis. */
static void Cmd_MEM(int argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    printk("--- THREAD STACK USAGE ---\n");
    thread_analyzer_print();
}

// ----------------------------------------------------------------------------
/** @brief Command handler for UART manual transmission. */
static void Cmd_UART(int argc, char **argv)
{
    if (argc < 1) { return; }

    if (strcmp(argv[0], "TX") == 0 && argc == 2) {
        /* Delegate to ext_interfaces.c which owns the usart3 handle */
        EXT_UART_Tx(3, argv[1]); /* bus index 3 = usart3 */
        printk("LOG: MCU transmitted UART '%s' (PB2 -> CON_15)\n", argv[1]);
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for direct ADC hardware reads. */
static void Cmd_ADC(int argc, char **argv)
{
    if (argc < 1) { return; }

    if (strcmp(argv[0], "READ") == 0) {
        /* Delegate to ext_interfaces so ADC device state stays centralised */
        EXT_ADC_Read("AIN9"); /* default channel — extend as needed */
    }
}



// ----------------------------------------------------------------------------
/** @brief Command handler for generic External Interface hardware control. */
static void Cmd_EXT(int argc, char **argv)
{
    if (argc < 2) { return; }

    if (strcmp(argv[0], "UART") == 0 && argc == 3) {
        EXT_UART_Tx(atoi(argv[1]), argv[2]);

    } else if (strcmp(argv[0], "I2C") == 0 && argc == 4) {
        EXT_I2C_Write(atoi(argv[1]),
                      (uint8_t)strtol(argv[2], NULL, 16),
                      (uint8_t)strtol(argv[3], NULL, 16));

    } else if (strcmp(argv[0], "GPIO") == 0) {
        if (argc == 4 && strcmp(argv[2], "TOGGLE") == 0) {
            EXT_GPIO_Toggle(argv[1], atoi(argv[3]));
        } else if (argc == 3) {
            EXT_GPIO_Write(argv[1], atoi(argv[2]));
        } else {
            printk("ERR: Invalid EXT:GPIO syntax.\n");
        }

    } else if (strcmp(argv[0], "ADC") == 0 && argc == 2) {
        EXT_ADC_Read(argv[1]);

    } else if (strcmp(argv[0], "DAC") == 0) {
        if (argc == 2 && strcmp(argv[1], "OFF") == 0) {
            EXT_DAC_Stop();
            printk("LOG: DAC Output disabled.\n");
        } else if (argc == 2) {
            EXT_DAC_Set((float)atof(argv[1]));
            printk("LOG: DAC DC Output set.\n");
        } else if (argc == 7 && strcmp(argv[1], "WAVE") == 0) {
            int wave = 0;
            if      (strcmp(argv[2], "SINE")     == 0) { wave = 1; }
            else if (strcmp(argv[2], "TRIANGLE") == 0) { wave = 2; }
            else if (strcmp(argv[2], "SQUARE")   == 0) { wave = 3; }
            uint32_t freq  = (uint32_t)strtoul(argv[3], NULL, 10);
            float    offs  = (float)atof(argv[4]);
            float    amp   = (float)atof(argv[5]);
            float    vref  = (float)atof(argv[6]);
            EXT_DAC_Wave((uint8_t)wave, freq, offs, amp, vref);
            printk("LOG: DAC DMA Waveform Configured.\n");
        }

    } else if (strcmp(argv[0], "AD9837") == 0) {
        if (argc == 2 && strcmp(argv[1], "OFF") == 0) {
            EXT_AD9837_Stop();
            printk("LOG: AD9837 disabled.\n");
        } else if (argc == 3) {
            int wave = 0;
            if      (strcmp(argv[1], "TRIANGLE") == 0) { wave = 1; }
            else if (strcmp(argv[1], "SQUARE")   == 0) { wave = 2; }
            EXT_AD9837_Set((uint8_t)wave,
                           (uint32_t)strtoul(argv[2], NULL, 10));
            printk("LOG: AD9837 Configured via SPI.\n");
        }

    } else if (strcmp(argv[0], "EXT_DAC") == 0 && argc == 2) {
        EXT_DAC081_Set((float)atof(argv[1]));

    } else if (strcmp(argv[0], "SCAN") == 0 && argc == 2) {
        EXT_I2C_Scanner(atoi(argv[1]));

    } else {
        printk("ERR: Unknown EXT interface command.\n");
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for executing system Benchmark sequences. */
static void Cmd_BENCHMARK(int argc, char **argv)
{
    if (argc < 1) { return; }

    if (strcmp(argv[0], "NATIVE") == 0) {
        const uint32_t iterations = 10;
        printk("LOG: Starting Mixed-Signal Native Benchmark...\n");

        uint32_t t_start = k_uptime_get_32();

        for (uint32_t i = 0; i < iterations; i++) {
            /* 1. SPI PING to FPGA */
            FpgaSpiPacket_t tx = {0};
            tx.sync = FPGA_SYNC_BYTE;
            tx.cmd  = 0x01;
            tx.length = 0;
            FPGA_EnqueueSpiPacket(&tx);

            /* 2. ADC READ — routed through ext_interfaces */
            EXT_ADC_Read("AIN9");

            /* 3. UART TX — routed through ext_interfaces */
            EXT_UART_Tx(3, "B");

            /* 4. GPIO TOGGLE — routed through ext_interfaces */
            EXT_GPIO_Write("PB11", 1);
            EXT_GPIO_Write("PB11", 0);
        }

        uint32_t duration = k_uptime_get_32() - t_start;
        printk("BENCH_RES:MCU:%u\n", (unsigned int)duration);

    } else if (strcmp(argv[0], "JS_END") == 0) {
        printk("BENCH_RES:JS_END:0\n");
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for inducing deliberate thread execution delays. */
static void Cmd_DELAY(int argc, char **argv)
{
    if (argc == 1) {
        k_msleep(atoi(argv[0]));
    }
}

// ----------------------------------------------------------------------------
/** @brief Command handler for running sequential Macros from the SD card. */
static void Cmd_MACRO(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[0], "RUN") == 0) {
        char fpath[SD_MAX_PATH_LEN];
        snprintf(fpath, sizeof(fpath), "%s/%s", SD_MOUNT_POINT, argv[1]);

        struct fs_file_t f;
        fs_file_t_init(&f);

        if (fs_open(&f, fpath, FS_O_READ) == 0) {
            char     line[CMD_BUF_LEN];
            uint32_t t_start = k_uptime_get_32();
            printk("LOG: Executing SD Macro '%s'...\n", argv[1]);

            /* Read line-by-line via fs_read looking for newlines */
            char  ch;
            int   li = 0;
            while (fs_read(&f, &ch, 1) == 1) {
                if (ch == '\n' || ch == '\r') {
                    if (li == 0) { continue; }
                    line[li] = '\0';
                    li = 0;
                    if (line[0] == '#') { continue; }
                    Frontend_ExecuteCommand(line);
                } else if (li < (int)(sizeof(line) - 1)) {
                    line[li++] = ch;
                }
            }
            /* Flush last line if file doesn't end with newline */
            if (li > 0) {
                line[li] = '\0';
                if (line[0] != '#') { Frontend_ExecuteCommand(line); }
            }

            fs_close(&f);
            printk("BENCH_RES:MACRO:%u\n",
                   (unsigned int)(k_uptime_get_32() - t_start));
        } else {
            printk("ERR: Macro file '%s' not found on SD.\n", argv[1]);
        }
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Parse and dispatch a single null-terminated command string.
 *
 * Tokens are separated by ':'. The first token selects the command handler;
 * remaining tokens are passed as argv[].
 *
 * Example:  "BQ:ICHG:500"  ->  Cmd_BQ(argc=2, argv={"ICHG","500"})
 */
void Frontend_ExecuteCommand(char *cmd_str)
{
    char *tokens[CMD_MAX_TOKENS];
    int   token_count = 0;
    char *p = cmd_str;

    while (*p != '\0' && token_count < CMD_MAX_TOKENS) {
        tokens[token_count++] = p;
        char *colon = strchr(p, ':');
        if (colon) { *colon = '\0'; p = colon + 1; }
        else       { break; }
    }

    if (token_count == 0) { return; }

    bool found = false;
    for (int j = 0; command_table[j].cmd != NULL; j++) {
        if (strcmp(tokens[0], command_table[j].cmd) == 0) {
            command_table[j].handler(token_count - 1, &tokens[1]);
            found = true;
            break;
        }
    }

    if (!found) {
        printk("ERR: Unknown command '%s'\n", tokens[0]);
    }
}

/**
 * @brief Process a raw USB RX block.
 *
 * Call this from your USB CDC-ACM RX callback (usb_task.c) each time a
 * chunk of bytes arrives. Handles both command mode and binary upload mode.
 *
 * @param data  Pointer to received byte buffer.
 * @param len   Number of valid bytes in @p data.
 */
void Frontend_ProcessBlock(const uint8_t *data, uint16_t len)
{
    uint16_t i = 0;

    while (i < len) {

        if (current_upload_state == UPLOAD_IDLE) {
            uint8_t c = data[i++];

            if (c == '\n' || c == '\r') {
                if (cmd_idx == 0) { continue; }
                cmd_buffer[cmd_idx] = '\0';
                Frontend_ExecuteCommand(cmd_buffer);
                cmd_idx = 0;
            } else if (cmd_idx < CMD_BUF_LEN - 1) {
                cmd_buffer[cmd_idx++] = c;
            }

        } else {
            /* Binary upload path */
            last_rx_time = k_uptime_get_32();

            uint32_t space_in_buf   = SD_BUF_SIZE - sd_write_idx;
            uint32_t bytes_remaining = upload_bytes_total - upload_bytes_received;
            uint32_t max_copy       = (space_in_buf < bytes_remaining)
                                      ? space_in_buf : bytes_remaining;
            uint32_t chunk          = (uint32_t)(len - i);
            uint32_t process_len    = (chunk < max_copy) ? chunk : max_copy;

            /* Update checksum */
            for (uint32_t k = 0; k < process_len; k++) {
                incoming_checksum ^= data[i + k];
            }

            memcpy(&sd_write_buffer[active_fill_buf][sd_write_idx],
                   &data[i], process_len);

            sd_write_idx            += process_len;
            upload_bytes_received   += process_len;
            bytes_since_last_ack    += process_len;
            i                       += process_len;

            /* Flush buffer when full or upload complete */
            if (sd_write_idx >= SD_BUF_SIZE ||
                upload_bytes_received == upload_bytes_total) {

                DiskWriteReq_t req = { active_fill_buf, (uint16_t)sd_write_idx };
                k_msgq_put(&disk_write_queue, &req, K_FOREVER);

                if (upload_bytes_received < upload_bytes_total) {
                    active_fill_buf = (active_fill_buf + 1) % 2;
                    sd_write_idx    = 0;
                    k_sem_take(&buffer_pool_sem, K_FOREVER);
                }
            }

            /* Flow-control ACK every 8 KiB */
            if (bytes_since_last_ack >= UPLOAD_CHUNK_ACK &&
                upload_bytes_received < upload_bytes_total) {
                printk("ACK_CHUNK\n");
                bytes_since_last_ack -= UPLOAD_CHUNK_ACK;
            }

            /* Upload finished */
            if (upload_bytes_received >= upload_bytes_total) {
                /* Wait for both write buffers to drain */
                k_sem_take(&buffer_pool_sem, K_FOREVER);
                k_sem_take(&buffer_pool_sem, K_FOREVER);
                fs_close(&upload_fil);
                current_upload_state = UPLOAD_IDLE;
                printk("ACK_DONE\n");
                printk("CSUM: 0x%02X\n", incoming_checksum);
                /* Return both tokens to pool */
                k_sem_give(&buffer_pool_sem);
                k_sem_give(&buffer_pool_sem);
            }
        }
    }
}

/**
 * @brief Upload watchdog — call periodically from your main loop or a timer.
 *
 * Aborts a stalled upload if no data has arrived within UPLOAD_TIMEOUT_MS.
 */
void Frontend_CheckTimeout(void)
{
    if (current_upload_state == UPLOAD_RECEIVING &&
        (k_uptime_get_32() - last_rx_time) > UPLOAD_TIMEOUT_MS) {

        fs_close(&upload_fil);
        current_upload_state = UPLOAD_IDLE;
        cmd_idx              = 0;
        printk("ERR_TIMEOUT\n");

        /* Drain any lingering sem tokens then restore to 2 */
        while (k_sem_take(&buffer_pool_sem, K_NO_WAIT) == 0) { /* drain */ }
        k_sem_give(&buffer_pool_sem);
        k_sem_give(&buffer_pool_sem);
    }
}

/**
 * @brief One-time module initialisation.
 *
 * Call once from main() after USB is enabled and the SD card is mounted.
 * The disk-write thread is started statically at boot (K_THREAD_DEFINE),
 * so this function is now a lightweight state reset.
 */
void Frontend_Init(void)
{
    current_upload_state  = UPLOAD_IDLE;
    cmd_idx               = 0;
    fs_file_t_init(&upload_fil);
}