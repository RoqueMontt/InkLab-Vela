/**
 * @file system_config.h
 * @brief Centralized system configurations and magic numbers (Law 3 Compliance).
 */
#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

/* --- Telemetry Configuration --- */
#define TELEM_RATE_DEFAULT_MS    1000

/* --- BQ25798 Charger Configuration --- */
#define BQ_I2C_ADDR              0x6B
#define BQ_VINDPM_DEFAULT_MV     4200
#define BQ_VOTG_BACKUP_MV        5000
#define BQ_VOTG_BASE_MV          2800
#define BQ_OTG_REG_DIVISOR       10
#define BQ_DEFAULT_ICHG_MA       500
#define BQ_ADC_CTRL_ENABLE       0x80
#define BQ_REG_DUMP_COUNT        48

/* --- Power Monitor (INA) Configuration --- */
#define INA_CURRENT_LSB_BASE     32768.0f
#define INA_CALIB_MAGIC          0.04096f
#define INA_DEFAULT_CONFIG       0x399F
#define POWER_MONITOR_ADDR_40    0x40
#define POWER_MONITOR_ADDR_43    0x43
#define POWER_MONITOR_ADDR_4C    0x4C
#define POWER_MONITOR_ADDR_4F    0x4F

#define INA_CURRENT_LSB_BASE     32768.0f

/* Hardware Shunt Resistors & Current Limits */
#define INA_CORE_RSHUNT          1.5f
#define INA_CORE_MAX_A           0.210f
#define INA_EXT_RSHUNT           1.0f
#define INA_EXT_MAX_A            0.320f
#define INA_FPGA_RSHUNT          1.0f
#define INA_FPGA_MAX_A           0.320f
#define INA_MCU_RSHUNT           1.0f
#define INA_MCU_MAX_A            0.320f
#define INA_FPGA_OFFSET_MA       0.47f

/* Auto-Scale Thresholds */
#define INA_BUS_V_MULTIPLIER     0.004f
#define INA_SHUNT_MV_MULTIPLIER  0.01f
#define INA_AUTO_UP_320MV        150.0f
#define INA_AUTO_UP_160MV        75.0f
#define INA_AUTO_UP_80MV         38.0f
#define INA_AUTO_DN_160MV        120.0f
#define INA_AUTO_DN_80MV         60.0f
#define INA_AUTO_DN_40MV         30.0f

/* --- Battery SOC Configuration --- */
#define BATT_DEFAULT_R_INT_OHMS  0.150f
#define BATT_SOC_MIN_DELTA_I     0.2f
#define BATT_SOC_MIN_R_INT       0.02f
#define BATT_SOC_MAX_R_INT       0.5f
#define BATT_SOC_R_EMA_W         0.2f
#define BATT_SOC_SOC_EMA_W       0.05f

/* --- Standard Battery Profiles --- */
#define BATT_LIION_3200_CHG_MV   4200
#define BATT_LIION_3200_CHG_MA   1600
#define BATT_LIION_3200_TERM_MA  80
#define BATT_LIION_3200_SYS_MV   3000

/* --- LED Default Configuration --- */
#define LED_DEFAULT_LIMIT_R      25
#define LED_DEFAULT_LIMIT_G      10
#define LED_DEFAULT_LIMIT_B      10

/* --- File System & Frontend Configuration --- */
#define SD_MOUNT_POINT           "/SD:"
#define SD_BUF_SIZE              8192U
#define UPLOAD_CHUNK_ACK         8192U
#define CMD_BUF_LEN              128U
#define CMD_MAX_TOKENS           10
#define UPLOAD_TIMEOUT_MS        3000U
#define SD_SLOT_OTA              99
#define SD_MAX_PATH_LEN          48U

/* --- FPGA Configuration --- */
#define FPGA_MAX_SLOTS           16
#define FPGA_SLOT_NAME_LEN       16
#define FPGA_SYNC_BYTE           0x5A
#define FPGA_RESET_ASSERT_MS     5
#define FPGA_RESET_RELEASE_MS    10
#define FPGA_TX_BUF_SIZE         4096U

/* --- USB RTOS Task Configuration --- */
#define USB_MSG_MAX_LEN          256
#define RX_RING_SIZE             8192
#define USB_TX_QUEUE_SIZE        64
#define USB_THREAD_STACK_SIZE    2048
#define USB_THREAD_PRIORITY      5
#define USB_TX_MAX_RETRIES       10

/* --- UART / USB CDC Configuration --- */
#define UART_RX_CHUNK_SIZE       64U
#define UART_TX_CHUNK_SIZE       128U

/* --- External Interface Configurations --- */
#define DAC_8BIT_MAX             255.0f
#define DAC_VREF_3V              3.0f
#define I2C_SCAN_MAX_ADDR        128

/* --- Task & Thread Configuration --- */
#define DISK_TASK_STACK          2048
#define DISK_TASK_PRIO           5
#define JOYSTICK_THREAD_STACK    1024
#define JOYSTICK_THREAD_PRIO     6
#define JOYSTICK_POLL_RATE_MS    50

/* --- Queues & Alignments --- */
#define SD_WRITE_QUEUE_SIZE      2
#define SD_WRITE_QUEUE_ALIGN     4
#define FPGA_TX_QUEUE_SIZE       8
#define FPGA_TX_QUEUE_ALIGN      4

/* --- Timing & Delays --- */
#define SYS_BOOT_DELAY_MS        2500
#define BQ_BACKUP_ARM_DELAY_MS   5000
#define BQ_BACKUP_REARM_DELAY_MS 3000
#define ERR_LED_HOLD_TIME_MS     2000
#define LED_PWM_PERIOD_MS        10
#define LED_PWM_MAX              100
#define SYS_WAKE_DELAY_MS        50

/* --- Diagnostic Configuration --- */
#define DIAG_I2C_TEST_ITERATIONS 1000
#define DIAG_I2C_YIELD_MODULUS   50
#define DIAG_I2C_EXPECTED_READS  4000.0f

#endif /* SYSTEM_CONFIG_H */