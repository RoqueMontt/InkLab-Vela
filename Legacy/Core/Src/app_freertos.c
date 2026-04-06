/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : app_freertos.c
  * @brief          : Cleaned and Decoupled RTOS Application Entry
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "usbd_cdc_if.h"
#include "app_fatfs.h"
#include "fpga.h"
#include "frontend_api.h"
#include "bq25798.h"
#include "powerMonitor.h"

// --- New Modular Libraries ---
#include "battery_soc.h"
#include "sys_power.h"
#include "led_manager.h"
#include "joystick.h"
#include "diagnostics.h"
#include "error_manager.h"
#include "pinmux.h"
#include "main.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define USB_MSG_MAX_LEN 256
typedef struct {
    char data[USB_MSG_MAX_LEN];
} UsbMsg_t;

typedef enum {
    TARGET_NONE,
    TARGET_INA_1V2,
    TARGET_INA_3V3EXT,
    TARGET_INA_3V3FPGA,
    TARGET_INA_3V3MCU,
    TARGET_BQ_VBUS,
    TARGET_BQ_VBAT
} FastTarget_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUM_BITSTREAMS 16
#define RX_RING_SIZE   8192
#define MAX_NAME_LEN   16
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
osMessageQueueId_t UsbQueueHandle;
osThreadId_t UsbTaskHandle;
osThreadId_t JoystickTaskHandle;
osThreadId_t LedTaskHandle;
osThreadId_t TelemetryTaskHandle;
osThreadId_t FpgaTaskHandle;
osMessageQueueId_t FpgaTxQueueHandle;
FpgaSpiPacket_t fpga_rx_packet;

extern USBD_HandleTypeDef hUsbDeviceFS;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim1;
PowerMonitor_t ina_1v2_core, ina_3v3_ext, ina_3v3_fpga, ina_3v3_stm32;
extern osSemaphoreId_t FpgaDmaSemaphore;
osSemaphoreId_t UsbRxSemaphore = NULL;
osSemaphoreId_t UsbTxSemaphore = NULL;
osSemaphoreId_t I2c1DmaSemaphore = NULL;
osMutexId_t I2c1Mutex = NULL;

volatile uint8_t rx_ring_buf[RX_RING_SIZE];
volatile uint16_t rx_ring_head = 0;
uint16_t rx_ring_tail = 0;

uint8_t current_slot = 0;
uint8_t fpga_is_ready = 0;

FATFS FatFs;
uint8_t slot_clk_configs[NUM_BITSTREAMS] = {0};
char slot_names[NUM_BITSTREAMS][MAX_NAME_LEN] = {0};
volatile uint8_t sd_is_mounted = 0;
__attribute__((section(".noinit"))) uint8_t last_active_slot;

// --- Telemetry Variables ---
volatile uint8_t telem_is_muted = 0;
volatile uint32_t telem_rate_ms = 1000;
volatile FastTarget_t fast_ch1 = TARGET_NONE;
volatile FastTarget_t fast_ch2 = TARGET_NONE;
volatile uint8_t pending_bq_adc_bits = 0;
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void Init_App_RTOS(void);
void USB_Printf(const char *format, ...);
void StartUsbTask(void *argument);
void StartFpgaTask(void *argument);
void StartTelemetryTask(void *argument);
void StartJoystickTask(void *argument);
void StartLedTask(void *argument);
uint32_t Get_Safe_SPI_Prescaler(void);
/* USER CODE END FunctionPrototypes */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */

/**
 * Public setter called from frontend_api.c command parser
 */



// Define your I2C pins here (Update to match your STM32CubeIDE pinout)
#define I2C1_SCL_PORT GPIOB
#define I2C1_SCL_PIN  GPIO_PIN_6
#define I2C1_SDA_PORT GPIOB
#define I2C1_SDA_PIN  GPIO_PIN_7

// 1. Add this simple microsecond delay helper right above the recovery function
static void I2C_Delay_5us(void) {
    // A simple loop to waste CPU cycles (~5us at 64MHz).
    // This is safe to run inside an Interrupt context!
    for (volatile uint32_t i = 0; i < 100; i++) {
        __NOP();
    }
}

// 2. Update your I2C_Bus_Recovery function
void I2C_Bus_Recovery(I2C_HandleTypeDef *hi2c) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Disable the I2C peripheral to take manual control of the pins
    __HAL_I2C_DISABLE(hi2c);
    HAL_I2C_DeInit(hi2c);

    // 2. Configure SCL and SDA as standard Open-Drain GPIOs
    GPIO_InitStruct.Pin = I2C1_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C1_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = I2C1_SDA_PIN;
    HAL_GPIO_Init(I2C1_SDA_PORT, &GPIO_InitStruct);

    // 3. Release SDA (Set High)
    HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_SET);

    // 4. Send 9 clock pulses on SCL to force the BQ25798 to release SDA
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_RESET);
        I2C_Delay_5us(); // Replaced HAL_Delay
        HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
        I2C_Delay_5us(); // Replaced HAL_Delay
    }

    // 5. Generate a manual STOP condition (SCL High, then SDA Low to High)
    HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_RESET);
    I2C_Delay_5us();
    HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_RESET);
    I2C_Delay_5us();
    HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
    I2C_Delay_5us();
    HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_SET);
    I2C_Delay_5us();

    // 6. Re-initialize the I2C peripheral
    HAL_I2C_Init(hi2c);
}

void SPI3_SetBaudRate(uint32_t prescaler) {
    extern SPI_HandleTypeDef hspi3;
    __HAL_SPI_DISABLE(&hspi3);
    MODIFY_REG(hspi3.Instance->CR1, SPI_BAUDRATEPRESCALER_256, prescaler);
    __HAL_SPI_ENABLE(&hspi3);
}

uint32_t Get_Safe_SPI_Prescaler(void) {
    uint8_t divider = slot_clk_configs[current_slot];
    if (divider < 2) divider = 2;

    uint32_t target_pre = divider * 8; // Ensure SPI is at least 4x slower

    if (target_pre <= 2)   return SPI_BAUDRATEPRESCALER_2;
    if (target_pre <= 4)   return SPI_BAUDRATEPRESCALER_4;
    if (target_pre <= 8)   return SPI_BAUDRATEPRESCALER_8;
    if (target_pre <= 16)  return SPI_BAUDRATEPRESCALER_16;
    if (target_pre <= 32)  return SPI_BAUDRATEPRESCALER_32;
    if (target_pre <= 64)  return SPI_BAUDRATEPRESCALER_64;
    if (target_pre <= 128) return SPI_BAUDRATEPRESCALER_128;

    return SPI_BAUDRATEPRESCALER_256;
}

void Telemetry_SetFastTarget(uint8_t ch, const char* target) {
    FastTarget_t t = TARGET_NONE;
    if (strcmp(target, "1v2_core") == 0) t = TARGET_INA_1V2;
    else if (strcmp(target, "3v3_ext") == 0) t = TARGET_INA_3V3EXT;
    else if (strcmp(target, "3v3_fpga") == 0) t = TARGET_INA_3V3FPGA;
    else if (strcmp(target, "3v3_mcu") == 0) t = TARGET_INA_3V3MCU;
    else if (strcmp(target, "vbus") == 0) t = TARGET_BQ_VBUS;
    else if (strcmp(target, "vbat") == 0) t = TARGET_BQ_VBAT;

    if (ch == 1) fast_ch1 = t;
    else if (ch == 2) fast_ch2 = t;

    if (fast_ch1 == TARGET_BQ_VBUS || fast_ch1 == TARGET_BQ_VBAT ||
        fast_ch2 == TARGET_BQ_VBUS || fast_ch2 == TARGET_BQ_VBAT) {
        pending_bq_adc_bits = 13; // 6ms
    } else {
        pending_bq_adc_bits = 15; // 24ms
    }

    PowerMonitor_SetFastMode(&ina_1v2_core,  (fast_ch1 == TARGET_INA_1V2 || fast_ch2 == TARGET_INA_1V2));
    PowerMonitor_SetFastMode(&ina_3v3_ext,   (fast_ch1 == TARGET_INA_3V3EXT || fast_ch2 == TARGET_INA_3V3EXT));
    PowerMonitor_SetFastMode(&ina_3v3_fpga,  (fast_ch1 == TARGET_INA_3V3FPGA || fast_ch2 == TARGET_INA_3V3FPGA));
    PowerMonitor_SetFastMode(&ina_3v3_stm32, (fast_ch1 == TARGET_INA_3V3MCU || fast_ch2 == TARGET_INA_3V3MCU));
}

/* USER CODE END 4 */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void Init_App_RTOS(void) {
    const osMessageQueueAttr_t UsbQueue_attributes = { .name = "UsbQueue" };
    UsbQueueHandle = osMessageQueueNew(64, sizeof(UsbMsg_t), &UsbQueue_attributes);

    const osThreadAttr_t usbTask_attr = { .name = "UsbTask", .priority = (osPriority_t) osPriorityHigh, .stack_size = 1024 * 8 };
    UsbTaskHandle = osThreadNew(StartUsbTask, NULL, &usbTask_attr);

    const osThreadAttr_t fpgaTask_attr = { .name = "FpgaTask", .priority = (osPriority_t) osPriorityAboveNormal, .stack_size = 1024 * 4 };
    FpgaTaskHandle = osThreadNew(StartFpgaTask, NULL, &fpgaTask_attr);

    const osThreadAttr_t telemTask_attr = { .name = "TelemTask", .priority = (osPriority_t) osPriorityNormal, .stack_size = 1024 * 4 };
    TelemetryTaskHandle = osThreadNew(StartTelemetryTask, NULL, &telemTask_attr);

    const osThreadAttr_t joyTask_attr = { .name = "JoyTask", .priority = (osPriority_t) osPriorityLow, .stack_size = 512 * 4 };
    JoystickTaskHandle = osThreadNew(StartJoystickTask, NULL, &joyTask_attr);

    const osThreadAttr_t ledTask_attr = { .name = "LedTask", .priority = (osPriority_t) osPriorityLow, .stack_size = 128 * 4 };
    LedTaskHandle = osThreadNew(StartLedTask, NULL, &ledTask_attr);

    const osMessageQueueAttr_t FpgaTxQueue_attr = { .name = "FpgaTxQueue" };
    FpgaTxQueueHandle = osMessageQueueNew(8, sizeof(FpgaSpiPacket_t), &FpgaTxQueue_attr);

    const osSemaphoreAttr_t rx_sem_attr = { .name = "UsbRxSem" };
    UsbRxSemaphore = osSemaphoreNew(1, 0, &rx_sem_attr);

    const osSemaphoreAttr_t tx_sem_attr = { .name = "UsbTxSem" };
    // Create binary semaphore, initial count 1 (ready to TX)
    UsbTxSemaphore = osSemaphoreNew(1, 1, &tx_sem_attr);

    // Create binary semaphore, initial count 0 (empty)
    const osSemaphoreAttr_t i2c_sem_attr = { .name = "I2cDmaSem" };
    I2c1DmaSemaphore = osSemaphoreNew(1, 0, &i2c_sem_attr);

    const osMutexAttr_t i2c_mutex_attr = { .name = "I2cMutex" };
    I2c1Mutex = osMutexNew(&i2c_mutex_attr);

}

void USB_Printf(const char *format, ...) {
    if(hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return;
    UsbMsg_t msg;
    va_list args;
    va_start(args, format);
    vsnprintf(msg.data, sizeof(msg.data), format, args);
    va_end(args);
    if (UsbQueueHandle != NULL) osMessageQueuePut(UsbQueueHandle, &msg, 0, 0);
}

void Save_Configs(void) {
    if (!sd_is_mounted) return;
    FIL f; UINT bw;
    if (f_open(&f, "0:/sys.cfg", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        f_write(&f, slot_clk_configs, NUM_BITSTREAMS, &bw);
        f_write(&f, slot_names, NUM_BITSTREAMS * MAX_NAME_LEN, &bw);
        f_close(&f);
    }
}

void Load_Configs(void) {
    if (!sd_is_mounted) return;
    FIL f; UINT br;
    if (f_open(&f, "0:/sys.cfg", FA_READ) == FR_OK) {
        f_read(&f, slot_clk_configs, NUM_BITSTREAMS, &br);
        f_read(&f, slot_names, NUM_BITSTREAMS * MAX_NAME_LEN, &br);
        f_close(&f);
    }
}

void Apply_Slot_Clock(uint8_t slot) {
    uint8_t divider = slot_clk_configs[slot];
    if (divider < 2)   divider = 2;
    if (divider > 128) divider = 128;

    uint32_t mco_div; float freq_val;
    if (divider <= 1)      { mco_div = RCC_MCODIV_1;   freq_val = 64.0f; }
    else if (divider <= 2) { mco_div = RCC_MCODIV_2;   freq_val = 32.0f; }
    else if (divider <= 4) { mco_div = RCC_MCODIV_4;   freq_val = 16.0f; }
    else if (divider <= 8) { mco_div = RCC_MCODIV_8;   freq_val = 8.0f;  }
    else if (divider <= 16){ mco_div = RCC_MCODIV_16;  freq_val = 4.0f;  }
    else if (divider <= 32){ mco_div = RCC_MCODIV_32;  freq_val = 2.0f;  }
    else if (divider <= 64){ mco_div = RCC_MCODIV_64;  freq_val = 1.0f;  }
    else                   { mco_div = RCC_MCODIV_128; freq_val = 0.5f;  }

    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_SYSCLK, mco_div);

    const char* displayName = slot_names[slot];
    if (slot == 0 && (displayName[0] == '\0' || strcmp(displayName, "unconfigured") == 0)) {
        displayName = "System Zero";
    }
    USB_Printf("LIVE_CLK:%d:%.2f MHz:%s\n", slot, freq_val, displayName);
}

// ============================================================================
// MAIN TASKS
// ============================================================================

void StartUsbTask(void *argument) {
    while (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) osDelay(100);
    osDelay(500);

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    if (TAMP->BKP0R == 0x4F544150) {
		TAMP->BKP0R = 0x00000000;

		// Add this delay to allow USB descriptors to finish enumerating
		osDelay(2500);

		USB_Printf("OTA_LOG: === SYSTEM BOOTING (V2.1 OTA SUCCESS!) ===\n");
		USB_Printf("LOG: Rollback flag cleared. New firmware verified.\n");
	} else {
		USB_Printf("\n=== SYSTEM BOOTING ===\n");
	}

    Frontend_Init();
    static UsbMsg_t tx_msg;

    for(;;) {
        USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
        bool processed_rx = false;

        uint16_t current_head = rx_ring_head;
        if (rx_ring_tail != current_head) {
            uint16_t contiguous_len = (current_head > rx_ring_tail) ?
                                      (current_head - rx_ring_tail) : (RX_RING_SIZE - rx_ring_tail);

            Frontend_ProcessBlock((uint8_t*)&rx_ring_buf[rx_ring_tail], contiguous_len);
            rx_ring_tail = (rx_ring_tail + contiguous_len) % RX_RING_SIZE;
            processed_rx = true;
        }

        Frontend_CheckTimeout();

        if (hcdc != NULL) {
			// 1. Check if messages exist BEFORE taking the semaphore
			if (osMessageQueueGetCount(UsbQueueHandle) > 0) {

				// 2. Sleep until the USB hardware is idle
				if (osSemaphoreAcquire(UsbTxSemaphore, 100) == osOK) {

					// 3. NOW pull the message. It is safe to overwrite tx_msg.
					if (osMessageQueueGet(UsbQueueHandle, &tx_msg, NULL, 0) == osOK) {
						if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
							uint16_t len = strlen(tx_msg.data);
							if (len > USB_MSG_MAX_LEN - 1) len = USB_MSG_MAX_LEN - 1;

							// 4. Robust TX loop: Do NOT overwrite tx_msg until accepted
							uint8_t status;
							uint16_t timeout = 500; // 500ms safety net
							do {
								status = CDC_Transmit_FS((uint8_t*)tx_msg.data, len);
								if (status == USBD_BUSY) osDelay(1); // Yield to other tasks
								timeout--;
							} while (status == USBD_BUSY && timeout > 0);

							// If it failed or timed out, release the semaphore to prevent deadlocks.
							// If it succeeded (USBD_OK), the ISR will release it later.
							if (status != USBD_OK) {
								osSemaphoreRelease(UsbTxSemaphore);
							}
						} else {
							osSemaphoreRelease(UsbTxSemaphore); // USB dropped
						}
					} else {
						osSemaphoreRelease(UsbTxSemaphore); // Queue empty
					}
				}
			} else if (!processed_rx) {
				// No TX pending and no RX processed, sleep safely
				osSemaphoreAcquire(UsbRxSemaphore, 10);
			}
		}
    }
}

void StartFpgaTask(void *argument) {
    extern SPI_HandleTypeDef hspi3;
    uint8_t has_auto_programmed = 0;
    FpgaSpiPacket_t tx_packet;

    for(;;) {
    	// --- Auto-Boot Logic ---
		if (!has_auto_programmed && hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
			osDelay(500);

			uint8_t target_slot = (last_active_slot < 16) ? last_active_slot : 0;

			// ALWAYS wait for the SD Card, even for slot 0
			uint8_t sd_wait = 30;
			while (!sd_is_mounted && sd_wait > 0) { osDelay(100); sd_wait--; }

			USB_Printf("LOG: Auto-programming Slot %d...\n", target_slot);
			SPI3_SetBaudRate(SPI_BAUDRATEPRESCALER_4); // MUST BE FAST FOR PROGRAMMING

            if (FPGA_Program_Slot(target_slot) == HAL_OK) {
                fpga_is_ready = 1; current_slot = target_slot; last_active_slot = target_slot;
                Apply_Slot_Clock(target_slot);
            } else if (target_slot != 0) {
                if (FPGA_Program_Slot(0) == HAL_OK) {
                    fpga_is_ready = 1; current_slot = 0; last_active_slot = 0;
                    Apply_Slot_Clock(0);
                }
            }
            has_auto_programmed = 1;
        }

        // --- Standard UI Programming Flag ---
        if (osThreadFlagsGet() & 0x01) {
            osThreadFlagsClear(0x01);
            SPI3_SetBaudRate(SPI_BAUDRATEPRESCALER_4); // MUST BE FAST FOR PROGRAMMING

            USB_Printf("Programming Slot %d...\n", current_slot);
            if (FPGA_Program_Slot(current_slot) == HAL_OK) {
                fpga_is_ready = 1; last_active_slot = current_slot;
                Apply_Slot_Clock(current_slot);
            } else {
                fpga_is_ready = 0; HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
                USB_Printf("LIVE_CLK:%d:FAILED\n", current_slot);
            }
        }

        // --- SPI Diagnostic Commands ---
        if (osMessageQueueGet(FpgaTxQueueHandle, &tx_packet, NULL, 50) == osOK) {
            if (!fpga_is_ready) {
                USB_Printf("ERR: FPGA not ready.\n");
                continue;
            }

            while (osSemaphoreAcquire(FpgaDmaSemaphore, 0) == osOK);
            __HAL_SPI_CLEAR_OVRFLAG(&hspi3);

            // APPLY SAFE CLOCK RATIO BEFORE TALKING TO RUNNING FPGA
            SPI3_SetBaudRate(Get_Safe_SPI_Prescaler());

            // Handle background Benchmarks triggered from the USB task
            if (tx_packet.cmd == 0x99) { Diag_RunSPIBenchmark(); continue; }
            if (tx_packet.cmd == 0x9A) { Diag_RunSPIBulk(); continue; }

            HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_RESET);
            if (HAL_SPI_TransmitReceive_DMA(&hspi3, (uint8_t*)&tx_packet, (uint8_t*)&fpga_rx_packet, sizeof(FpgaSpiPacket_t)) == HAL_OK) {
                osSemaphoreAcquire(FpgaDmaSemaphore, 100);
            }
            HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_SET);

            if (fpga_rx_packet.sync == FPGA_SYNC_BYTE) {
				USB_Printf("{\"type\":\"spi\",\"cmd\":%d,\"len\":%d,\"p0\":%d,\"p1\":%d,\"p2\":%d,\"p3\":%d}\n",
						   tx_packet.cmd, fpga_rx_packet.length,
						   fpga_rx_packet.payload[0], fpga_rx_packet.payload[1],
						   fpga_rx_packet.payload[2], fpga_rx_packet.payload[3]);
			} else {
				USB_Printf("ERR: SPI_RX Sync Loss\n");
			}
        }
    }
}

static void Format_PowerMonitor_Json(const char* id, PowerMonitor_Data_t *data, char *buffer, size_t max_len) {
    snprintf(buffer, max_len, "{\"type\":\"ina\",\"id\":\"%s\",\"v\":%.2f,\"i\":%.2f,\"p\":%.4f}",
        id, data->bus_voltage_V, data->current_mA, data->power_mW);
}

void StartTelemetryTask(void *argument) {
    char json_buf[256]; char fast_json[128];
    BQ25798_Data_t bq_data; PowerMonitor_Data_t ina_data;

    BQ25798_Init(&hi2c1);
    PowerMonitor_Init(&ina_1v2_core, &hi2c1, POWER_MONITOR_ADDR_40, 1.5f, 0.210f);
    PowerMonitor_Init(&ina_3v3_ext,  &hi2c1, POWER_MONITOR_ADDR_43, 1.0f, 0.320f);
    PowerMonitor_Init(&ina_3v3_fpga, &hi2c1, POWER_MONITOR_ADDR_4C, 1.0f, 0.320f);
    PowerMonitor_SetOffset(&ina_3v3_fpga, 0.47f);
    PowerMonitor_Init(&ina_3v3_stm32,&hi2c1, POWER_MONITOR_ADDR_4F, 1.0f, 0.320f);

    BQ25798_SetBatteryProfile(&BATT_PROFILE_LIION_3200);
    BQ25798_EnableBackup5V5();
    BQ25798_SetOTGCurrent(1000);

    BatterySOC_Init();

    static uint8_t last_sd_state = 0xFF;
    uint32_t tick = 0xFFFFFFFF;
    static float c1_v = 0, c1_i = 0, c2_v = 0, c2_i = 0;

    for(;;) {
        tick++;
        if (pending_bq_adc_bits != 0) { BQ25798_SetADCResolution(pending_bq_adc_bits); pending_bq_adc_bits = 0; }

        bool send_fast = false; bool bq_already_read_this_tick = false;

        if (!SysPower_IsSleeping()) {
            if (tick % 4 == 0) {
                bool need_bq = (fast_ch1 == TARGET_BQ_VBUS || fast_ch1 == TARGET_BQ_VBAT ||
                                fast_ch2 == TARGET_BQ_VBUS || fast_ch2 == TARGET_BQ_VBAT);
                if (need_bq && BQ25798_ReadAll(&bq_data) == HAL_OK) {
                    bq_already_read_this_tick = true;
                    if (fast_ch1 == TARGET_BQ_VBUS) { c1_v = bq_data.vbus_V; c1_i = bq_data.ibus_mA; }
                    else if (fast_ch1 == TARGET_BQ_VBAT) { c1_v = bq_data.vbat_V; c1_i = bq_data.ibat_mA; }
                    if (fast_ch2 == TARGET_BQ_VBUS) { c2_v = bq_data.vbus_V; c2_i = bq_data.ibus_mA; }
                    else if (fast_ch2 == TARGET_BQ_VBAT) { c2_v = bq_data.vbat_V; c2_i = bq_data.ibat_mA; }
                    send_fast = true;
                }
            }

            #define READ_INA_FAST(enum, ptr) \
                if (fast_ch1 == enum || fast_ch2 == enum) { \
                    if (PowerMonitor_Read(ptr, &ina_data) == HAL_OK) { \
                        if (fast_ch1 == enum) { c1_v = ina_data.bus_voltage_V; c1_i = ina_data.current_mA; } \
                        if (fast_ch2 == enum) { c2_v = ina_data.bus_voltage_V; c2_i = ina_data.current_mA; } \
                        send_fast = true; \
                    } \
                }

            READ_INA_FAST(TARGET_INA_1V2, &ina_1v2_core); READ_INA_FAST(TARGET_INA_3V3EXT, &ina_3v3_ext);
            READ_INA_FAST(TARGET_INA_3V3FPGA, &ina_3v3_fpga); READ_INA_FAST(TARGET_INA_3V3MCU, &ina_3v3_stm32);

            if (send_fast && hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
                snprintf(fast_json, sizeof(fast_json), "{\"t\":\"f\",\"1\":[%.2f,%.2f],\"2\":[%.2f,%.2f]}\n", c1_v, c1_i, c2_v, c2_i);
                CDC_Transmit_FS((uint8_t*)fast_json, strlen(fast_json));
            }
        }

        uint32_t slow_ticks = SysPower_IsSleeping() ? 25 : ((telem_rate_ms < 5) ? 1 : (telem_rate_ms / 5));

        if (tick % slow_ticks == 0) {
            PowerMonitor_t* sensors[] = {&ina_1v2_core, &ina_3v3_ext, &ina_3v3_fpga, &ina_3v3_stm32};
            const char* sensor_ids[] = {"1v2_core", "3v3_ext", "3v3_fpga", "3v3_mcu"};

            if (SysPower_IsSleeping()) {
                for (int i = 0; i < 4; i++) PowerMonitor_TriggerSingleShot(sensors[i]);
                osDelay(1);
            }

            if (!bq_already_read_this_tick) BQ25798_ReadAll(&bq_data);

            float current_soc, calculated_r_int;
                        BatterySOC_Update(bq_data.vbat_V, bq_data.ibat_mA, &current_soc, &calculated_r_int);

			// --- BATTERY PROTECTION: Smart SOC Hysteresis ---
			static bool charge_paused_by_soc = false;
			if (!charge_paused_by_soc && current_soc >= 95.0f) {
				charge_paused_by_soc = true;
				BQ25798_SetChargeEnable(0);
				if (!telem_is_muted) USB_Printf("LOG: SOC reached 95%%. Charging paused to preserve battery.\n");
			} else if (charge_paused_by_soc && current_soc <= 90.0f) {
				charge_paused_by_soc = false;
				BQ25798_SetChargeEnable(1);
				if (!telem_is_muted) USB_Printf("LOG: SOC dropped to 90%%. Charging resumed.\n");
			}

			BQ25798_GetJson(&bq_data, json_buf, sizeof(json_buf));
            char* closing_brace = strrchr(json_buf, '}');
            if (closing_brace) {
                snprintf(closing_brace, sizeof(json_buf) - (closing_brace - json_buf),
                         ",\"soc\":%.1f,\"r_int\":%.3f}", current_soc, calculated_r_int);
            }

            if (!telem_is_muted) USB_Printf("%s\n", json_buf);

            // BQ Backup Reset Logic
            uint8_t vbus_stat = (bq_data.stat1 >> 1) & 0x0F;
            if ((vbus_stat == 0x0C) && (bq_data.stat0 & (1 << 1))) BQ25798_RearmBackup(3000);
            if (vbus_stat >= 0x01 && vbus_stat <= 0x06) {
                BQ25798_SetInputVoltageLimit(4600); // Keep pushing 4.6V
            }

            for (int i = 0; i < 4; i++) {
                if (PowerMonitor_Read(sensors[i], &ina_data) == HAL_OK) {
                    if (!SysPower_IsSleeping()) PowerMonitor_AutoScale(sensors[i], &ina_data);
                    Format_PowerMonitor_Json(sensor_ids[i], &ina_data, json_buf, sizeof(json_buf));
                    if (!telem_is_muted) USB_Printf("%s\n", json_buf);
                }
            }

            uint8_t current_sd_state = (HAL_GPIO_ReadPin(GPIOA, SD_DET_Pin) == GPIO_PIN_RESET) ? 1 : 0;
            if (current_sd_state != last_sd_state) {
                if (current_sd_state == 1) {
                    if (f_mount(&FatFs, "0:", 1) == FR_OK) {
                        sd_is_mounted = 1; Load_Configs(); USB_Printf("SYS: SD Card Mounted\r\n");
                    } else {
                        Error_Raise(ERR_SD_MOUNT_FAIL, SEV_WARNING, "f_mount returned error");
                    }
                } else if (last_sd_state != 0xFF) {
                    sd_is_mounted = 0; f_mount(NULL, "0:", 0); USB_Printf("SYS: SD Card Removed\r\n");
                }
                last_sd_state = current_sd_state;
            }
            USB_Printf("{\"type\":\"sys\",\"sd\":%d}\n", current_sd_state);

            // ---> ADD THIS NEW BLOCK <---
            char gpio_json[128];
            Pinmux_GetInputTelemetryJson(gpio_json, sizeof(gpio_json));
            if (gpio_json[0] != '\0' && !telem_is_muted) {
                USB_Printf("%s\n", gpio_json);
            }
        }
        osDelay(5);
    }
}

// ============================================================================
// PERIPHERAL TASKS
// ============================================================================

void StartJoystickTask(void *argument) {
    Joystick_Init();
    for(;;) {
        JoyAction_t action = Joystick_Process();

        if (action != JOY_NONE) {
            JoystickMap_t map = Joystick_GetMap();

            if (action == JOY_UP || action == JOY_DOWN) {
                if (map.ud == 0) {
                    uint32_t val = slot_clk_configs[current_slot];
                    if (action == JOY_UP && val > 2) val /= 2;
                    else if (action == JOY_DOWN && val < 128) val *= 2;
                    slot_clk_configs[current_slot] = val; Save_Configs(); Apply_Slot_Clock(current_slot);
                } else if (map.ud == 1) {
                    BQ25798_TogglePFM(); USB_Printf("LOG: BQ PFM Modes Toggled\n");
                }
            }
            else if (action == JOY_LEFT || action == JOY_RIGHT) {
                if (map.lr == 0) {
                    if (action == JOY_LEFT) current_slot = (current_slot == 0) ? NUM_BITSTREAMS - 1 : current_slot - 1;
                    else current_slot = (current_slot + 1) % NUM_BITSTREAMS;
                    USB_Printf("Slot: %d\n", current_slot);
                } else if (map.lr == 1) {
                    BQ25798_AdjustVOTG(action == JOY_RIGHT ? 100 : -100);
                    USB_Printf("LOG: BQ VOTG Adjusted\n");
                }
            }
            else if (action == JOY_CENTER) {
                if (map.cen == 0) osThreadFlagsSet(FpgaTaskHandle, 0x01);
                else if (map.cen == 1) { BQ25798_ToggleAutoRearm(); USB_Printf("LOG: BQ Auto-Rearm Toggled\n"); }
            }
        }
        osDelay(50);
    }
}

void StartLedTask(void *argument) {
    LED_Init();
    for(;;) {
        LED_ToggleHeartbeat();
        LED_SetFpgaReady(fpga_is_ready);
        osDelay(500);
    }
}

// Triggered when an I2C DMA Memory Write finishes
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    extern osSemaphoreId_t I2c1DmaSemaphore;
    if (hi2c->Instance == I2C1 && I2c1DmaSemaphore != NULL) {
        osSemaphoreRelease(I2c1DmaSemaphore);
    }
}

// Triggered when an I2C DMA Memory Read finishes
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    extern osSemaphoreId_t I2c1DmaSemaphore;
    if (hi2c->Instance == I2C1 && I2c1DmaSemaphore != NULL) {
        osSemaphoreRelease(I2c1DmaSemaphore);
    }
}

// Triggered if the device NACKs or the bus hangs
// Triggered if the device NACKs or the bus hangs
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    extern osSemaphoreId_t I2c1DmaSemaphore;
    if (hi2c->Instance == I2C1) {

        // 1. Recover the hung bus
        I2C_Bus_Recovery(hi2c);

        // 2. Release semaphore so the telemetry task doesn't freeze forever
        if (I2c1DmaSemaphore != NULL) {
            osSemaphoreRelease(I2c1DmaSemaphore);
        }
    }
}

/* USER CODE END Application */

