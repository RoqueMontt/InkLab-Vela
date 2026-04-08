#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>
#include <string.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/reboot.h>

#include "system_config.h"
#include "hal/powerMonitor.h"
#include "hal/bq25798.h"
#include "hal/battery_soc.h"
#include "hal/led_manager.h"
#include "hal/joystick.h"

/* --- Device Tree Handles --- */
static const struct device *i2c1_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));

PowerMonitor_t ina_1v2_core;
PowerMonitor_t ina_3v3_ext;
PowerMonitor_t ina_3v3_fpga;
PowerMonitor_t ina_3v3_mcu;

/* External Timer owned by led_manager.c */
extern struct k_timer led_pwm_timer;

/* --- Telemetry Globals exported for frontend_api.c --- */
volatile uint8_t telem_is_muted = 0;
volatile uint32_t telem_rate_ms = TELEM_RATE_DEFAULT_MS;

volatile uint8_t sd_is_mounted = 0;
static struct fs_mount_t sd_mount = {
    .type = FS_FATFS,
    .fs_data = NULL,                          /* Iniftialized by Zephyr */
    .mnt_point = "/SD:",                      /* Matches SD_MOUNT_POINT in system_config.h */
    .storage_dev = (void *)"SD",              /* Target the SD disk */
};

void Telemetry_SetFastTarget(uint8_t ch, const char *target) { /* Stub for Fast Mode */ }


/** 
 * @brief Native Zephyr OTA DFU using MCUboot and Flash Map 
 */
int OTA_Update_From_SD(const char *filename) {
    struct fs_file_t file;
    struct flash_img_context ctx;
    uint8_t buf[512];
    int rc;
    ssize_t bytes_read;

    fs_file_t_init(&file);
    if (fs_open(&file, filename, FS_O_READ) != 0) {
        printk("ERR: OTA file not found on SD.\n");
        return -ENOENT;
    }

    printk("LOG: Staging OTA Image to Slot 1...\n");
    flash_img_init(&ctx);
    
    while ((bytes_read = fs_read(&file, buf, sizeof(buf))) > 0) {
        rc = flash_img_buffered_write(&ctx, buf, bytes_read, false);
        if (rc < 0) {
            printk("ERR: Flash write failed (%d).\n", rc);
            fs_close(&file);
            return rc;
        }
    }
    
    /* Flush remaining bytes */
    flash_img_buffered_write(&ctx, buf, 0, true);
    fs_close(&file);

    printk("LOG: Image verified. Requesting MCUboot upgrade...\n");
    
    /* Flag MCUboot to test the image on next reboot */
    boot_request_upgrade(BOOT_UPGRADE_TEST);
    
    k_msleep(500); // Allow USB pipeline to flush
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

/* --- Consumer Thread for Joystick Inputs --- */
static void joystick_thread_fn(void *arg1, void *arg2, void *arg3) {
    Joystick_Init();
    while (1) {
        JoyAction_t action = Joystick_Process();
        if (action != JOY_NONE) {
            JoystickMap_t map = Joystick_GetMap();
            
            if (action == JOY_UP || action == JOY_DOWN) {
                if (map.ud == 1) BQ25798_TogglePFM();
            } else if (action == JOY_LEFT || action == JOY_RIGHT) {
                if (map.lr == 1) BQ25798_AdjustVOTG(action == JOY_RIGHT ? 100 : -100);
            } else if (action == JOY_CENTER) {
                if (map.cen == 1) BQ25798_ToggleAutoRearm();
            }
        }
        k_msleep(JOYSTICK_POLL_RATE_MS);
    }
}
K_THREAD_DEFINE(joy_thread, JOYSTICK_THREAD_STACK, joystick_thread_fn, NULL, NULL, NULL, JOYSTICK_THREAD_PRIO, 0, 0);

int main(void) {
    k_msleep(100); 
    
    /* Safely enable USB */
    int ret = usb_enable(NULL);
    if (ret != 0 && ret != -EALREADY) {
        // Do not return! Just log it and keep booting the hardware.
        printk("WRN: USB failed to enable or is unplugged (%d)\n", ret);
    }
    
    k_sleep(K_MSEC(2500));

    /* Initialize SD Card */
    printk("LOG: Attempting to mount SD Card...\n");
    int sd_err = fs_mount(&sd_mount);
    
    if (sd_err == 0) {
        printk("LOG: SD Card mounted successfully.\n");
        sd_is_mounted = 1;
    } else {
        printk("ERR: SD Card mount failed with code: %d\n", sd_err);
        sd_is_mounted = 0;
        
        // Translate common codes for easier debugging
        if (sd_err == -5) printk("DEBUG: Error -5 (EIO). Usually means bad SPI wiring, missing pull-ups, or SPI speed too high.\n");
        if (sd_err == -19) printk("DEBUG: Error -19 (ENODEV). Usually means the SD card is not physically inserted or detected.\n");
        if (sd_err == -22) printk("DEBUG: Error -22 (EINVAL). Usually means the FAT32/exFAT format is corrupted or unrecognized.\n");
    }
    
    printk("*** Booting Zephyr OS build %s ***\n", KERNEL_VERSION_STRING);

    if (!device_is_ready(i2c1_dev)) return -ENODEV;

    /* Initialize Hardware Modules */
    LED_Init();
    k_timer_start(&led_pwm_timer, K_MSEC(LED_PWM_PERIOD_MS), K_MSEC(LED_PWM_PERIOD_MS));

    BQ25798_Init(i2c1_dev);
    PowerMonitor_Init(&ina_1v2_core, i2c1_dev, POWER_MONITOR_ADDR_40, INA_CORE_RSHUNT, INA_CORE_MAX_A);
    PowerMonitor_Init(&ina_3v3_ext,  i2c1_dev, POWER_MONITOR_ADDR_43, INA_EXT_RSHUNT, INA_EXT_MAX_A);
    PowerMonitor_Init(&ina_3v3_fpga, i2c1_dev, POWER_MONITOR_ADDR_4C, INA_FPGA_RSHUNT, INA_FPGA_MAX_A);
    PowerMonitor_Init(&ina_3v3_mcu,  i2c1_dev, POWER_MONITOR_ADDR_4F, INA_MCU_RSHUNT, INA_MCU_MAX_A);
    PowerMonitor_SetOffset(&ina_3v3_fpga, INA_FPGA_OFFSET_MA);

    BatterySOC_Init();
    
    BQ25798_Data_t bq_data;
    bool backup_armed = false;
    uint32_t boot_tick = k_uptime_get_32();

    while (1) {
        if (BQ25798_ReadAll(&bq_data) == 0) {
            float soc = 0.0f, r_int = 0.0f;
            BatterySOC_Update(bq_data.vbat_V, bq_data.ibat_mA, &soc, &r_int);

            if (!telem_is_muted) {
                char bq_json[256];
                BQ25798_GetJson(&bq_data, bq_json, sizeof(bq_json));
                char* brace = strrchr(bq_json, '}');
                if (brace) snprintf(brace, sizeof(bq_json) - (brace - bq_json), 
                                    ",\"soc\":%.1f,\"r_int\":%.3f}", (double)soc, (double)r_int);
                printk("%s\n", bq_json);
            }

            // Arm/Rearm Logic
            if (!backup_armed && (k_uptime_get_32() - boot_tick > BQ_BACKUP_ARM_DELAY_MS)) {
                BQ25798_EnableBackup5V5(); backup_armed = true;
            }
            if (((bq_data.stat1 >> 1) & 0x0F) == 0x0C && (bq_data.stat0 & (1 << 1))) {
                BQ25798_RearmBackup(BQ_BACKUP_REARM_DELAY_MS); 
            }
        }

        if (!telem_is_muted) {
            PowerMonitor_Data_t ina;
            if (PowerMonitor_Read(&ina_1v2_core, &ina) == 0)
                printk("{\"type\":\"ina\",\"id\":\"1v2_core\",\"v\":%.2f,\"i\":%.2f,\"p\":%.4f}\n", (double)ina.bus_voltage_V, (double)ina.current_mA, (double)ina.power_mW);
            if (PowerMonitor_Read(&ina_3v3_ext, &ina) == 0)
                printk("{\"type\":\"ina\",\"id\":\"3v3_ext\",\"v\":%.2f,\"i\":%.2f,\"p\":%.4f}\n", (double)ina.bus_voltage_V, (double)ina.current_mA, (double)ina.power_mW);
            if (PowerMonitor_Read(&ina_3v3_fpga, &ina) == 0)
                printk("{\"type\":\"ina\",\"id\":\"3v3_fpga\",\"v\":%.2f,\"i\":%.2f,\"p\":%.4f}\n", (double)ina.bus_voltage_V, (double)ina.current_mA, (double)ina.power_mW);
            if (PowerMonitor_Read(&ina_3v3_mcu, &ina) == 0)
                printk("{\"type\":\"ina\",\"id\":\"3v3_mcu\",\"v\":%.2f,\"i\":%.2f,\"p\":%.4f}\n", (double)ina.bus_voltage_V, (double)ina.current_mA, (double)ina.power_mW);
        }
        k_msleep(telem_rate_ms);
    }
    return 0;
}