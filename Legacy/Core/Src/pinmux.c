#include "pinmux.h"
#include "app_fatfs.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "cmsis_os.h" // Ensure this is at the top of the file


extern void USB_Printf(const char *format, ...);

// Internal representation of a Pin Mapping
typedef struct {
    const char* pinName;
    GPIO_TypeDef* port;
    uint16_t pin;
    uint32_t adcChannel; // 0xFF if not ADC capable
} PinMap_t;

typedef struct {
    osTimerId_t timer;
    const PinMap_t* pin;
} ToggleTimer_t;

// We need exactly one timer slot for every MCU pin
#define NUM_MCU_PINS 8
static ToggleTimer_t toggle_timers[NUM_MCU_PINS] = {0};

// The static definition of our physical MCU pins
static const PinMap_t mcuPins[NUM_MCU_PINS] = {
    {"PA5", GPIOA, GPIO_PIN_5, ADC_CHANNEL_5},
    {"PA6", GPIOA, GPIO_PIN_6, ADC_CHANNEL_6},
    {"PA7", GPIOA, GPIO_PIN_7, ADC_CHANNEL_7},
    {"PB0", GPIOB, GPIO_PIN_0, ADC_CHANNEL_8},
    {"PB1", GPIOB, GPIO_PIN_1, ADC_CHANNEL_9},
    {"PB2", GPIOB, GPIO_PIN_2, ADC_CHANNEL_10},
    {"B10", GPIOB, GPIO_PIN_10, ADC_CHANNEL_11},
    {"B11", GPIOB, GPIO_PIN_11, ADC_CHANNEL_15}
};

typedef enum {
    PINMUX_MODE_RESET,
    PINMUX_MODE_INPUT,
    PINMUX_MODE_OUTPUT,
    PINMUX_MODE_AF
} PinMode_t;

static uint32_t dac_wave_lut[32];
static TIM_HandleTypeDef htim_dac = {0};
static DMA_HandleTypeDef hdma_dac = {0};

static PinMode_t active_pin_modes[NUM_MCU_PINS] = {0};

static int GetPinIndex(const char* pinName) {
    for (int i = 0; i < NUM_MCU_PINS; i++) {
        if (strcmp(mcuPins[i].pinName, pinName) == 0) return i;
    }
    return -1;
}

static const PinMap_t* GetPinInfo(const char* pinName) {
    int idx = GetPinIndex(pinName);
    if (idx >= 0) return &mcuPins[idx];
    return NULL;
}

// ==========================================
// PIN ROUTING CORE
// ==========================================

HAL_StatusTypeDef Pinmux_SetPin(const char* pinName, const char* funcName) {
    int pinIdx = GetPinIndex(pinName);
    if (pinIdx < 0) return HAL_ERROR;

    const PinMap_t* pInfo = &mcuPins[pinIdx];
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pInfo->pin;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    // 1. Reset State
    if (strcmp(funcName, "Reset_State") == 0) {
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(pInfo->port, &GPIO_InitStruct);
        active_pin_modes[pinIdx] = PINMUX_MODE_RESET;
        return HAL_OK;
    }

    // 2. Standard GPIO
    if (strcmp(funcName, "GPIO_Input") == 0) {
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
        HAL_GPIO_Init(pInfo->port, &GPIO_InitStruct);
        active_pin_modes[pinIdx] = PINMUX_MODE_INPUT;
        return HAL_OK;
    }

    if (strcmp(funcName, "GPIO_Output") == 0) {
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(pInfo->port, &GPIO_InitStruct);
        active_pin_modes[pinIdx] = PINMUX_MODE_OUTPUT;
        return HAL_OK;
    }

    // 3. Alternate Functions (EXPANDED TO MATCH VUE UI)
    active_pin_modes[pinIdx] = PINMUX_MODE_AF;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    if (strstr(funcName, "USART3")) {
        __HAL_RCC_USART3_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF4_USART3;
    }
    else if (strstr(funcName, "USART5")) {
        __HAL_RCC_USART5_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF8_USART5;
    }
    else if (strstr(funcName, "SPI1")) {
        __HAL_RCC_SPI1_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;
    }
    else if (strstr(funcName, "I2C2")) {
        __HAL_RCC_I2C2_CLK_ENABLE();
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_PULLUP;      // <--- THIS ENABLES DIGITAL PULLUPS
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; // Lower speed is more stable for internal pullups
        GPIO_InitStruct.Alternate = GPIO_AF6_I2C2;
    }
    else if (strstr(funcName, "I2C3")) {
        __HAL_RCC_I2C3_CLK_ENABLE();
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Alternate = GPIO_AF6_I2C3;
    }
    else if (strstr(funcName, "FDCAN2")) {
        __HAL_RCC_FDCAN_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF3_FDCAN2;
    }
    else if (strstr(funcName, "TIM2")) {
        __HAL_RCC_TIM2_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF2_TIM2;
    }
    else if (strstr(funcName, "TIM3")) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF1_TIM3;
    }
    else if (strstr(funcName, "TIM14")) {
        __HAL_RCC_TIM14_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF4_TIM14;
    }
    else if (strstr(funcName, "TIM16")) {
        __HAL_RCC_TIM16_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF5_TIM16;
    }
    else if (strstr(funcName, "TIM17")) {
        __HAL_RCC_TIM17_CLK_ENABLE();
        GPIO_InitStruct.Alternate = GPIO_AF5_TIM17;
    }
    else if (strstr(funcName, "ADC1") || strstr(funcName, "DAC1")) {
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    }
    else {
        USB_Printf("ERR: Unsupported function %s\n", funcName);
        return HAL_ERROR;
    }

    HAL_GPIO_Init(pInfo->port, &GPIO_InitStruct);
    return HAL_OK;
}

// ==========================================
// TELEMETRY GENERATION
// ==========================================

void Pinmux_GetInputTelemetryJson(char* buffer, size_t max_len) {
    int offset = snprintf(buffer, max_len, "{\"type\":\"mcu_gpio\"");
    bool has_inputs = false;

    for (int i = 0; i < NUM_MCU_PINS; i++) {
        if (active_pin_modes[i] == PINMUX_MODE_INPUT) {
            uint8_t state = (HAL_GPIO_ReadPin(mcuPins[i].port, mcuPins[i].pin) == GPIO_PIN_SET) ? 1 : 0;
            offset += snprintf(buffer + offset, max_len - offset, ",\"%s\":%d", mcuPins[i].pinName, state);
            has_inputs = true;
        }
    }

    if (!has_inputs) {
        buffer[0] = '\0';
    } else {
        snprintf(buffer + offset, max_len - offset, "}");
    }
}

// ==========================================
// AGNOSTIC EXTERNAL COMMANDS (EXT:...)
// ==========================================

void EXT_UART_Tx(uint8_t uart_num, const char* payload) {
    if (uart_num == 3) {
        extern UART_HandleTypeDef huart3;
        HAL_UART_Transmit(&huart3, (uint8_t*)payload, strlen(payload), 100);
        USB_Printf("LOG: UART3 TX -> '%s'\n", payload);
    } else {
        USB_Printf("ERR: UART%d not configured.\n", uart_num);
    }
}

void EXT_I2C_Write(uint8_t i2c_num, uint8_t addr, uint8_t data) {
    if (i2c_num == 2) {
        extern I2C_HandleTypeDef hi2c2;
        if (HAL_I2C_Master_Transmit(&hi2c2, addr << 1, &data, 1, 100) == HAL_OK) {
            USB_Printf("LOG: I2C2 Write 0x%02X to Dev 0x%02X Success\n", data, addr);
        } else {
            USB_Printf("ERR: I2C2 NACK or Bus Error\n");
        }
    }
}

void EXT_GPIO_Write(const char* pinName, uint8_t state) {
    int pinIdx = GetPinIndex(pinName);
    if (pinIdx < 0) { USB_Printf("ERR: Unknown Pin %s\n", pinName); return; }

    const PinMap_t* pInfo = &mcuPins[pinIdx];
    ToggleTimer_t* slot = &toggle_timers[pinIdx];

    // If a toggle timer is running, stop it
    if (slot->timer) {
        osTimerStop(slot->timer);
        osTimerDelete(slot->timer);
        slot->timer = NULL;
    }

    // Force pin to output if it isn't already!
    if (active_pin_modes[pinIdx] != PINMUX_MODE_OUTPUT) {
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = pInfo->pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(pInfo->port, &GPIO_InitStruct);
        active_pin_modes[pinIdx] = PINMUX_MODE_OUTPUT;
    }

    HAL_GPIO_WritePin(pInfo->port, pInfo->pin, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    USB_Printf("LOG: %s set to %s\n", pinName, state ? "HIGH" : "LOW");
}

void EXT_ADC_Read(const char* pinName) {
    extern ADC_HandleTypeDef hadc1;
    const PinMap_t* pInfo = GetPinInfo(pinName);

    if (!pInfo || pInfo->adcChannel == 0xFF) {
        USB_Printf("ERR: %s is not ADC capable.\n", pinName);
        return;
    }

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = pInfo->adcChannel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) == HAL_OK) {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
            uint32_t raw = HAL_ADC_GetValue(&hadc1);
            float voltage = ((float)raw / 4095.0f) * 3.3f;
            USB_Printf("{\"type\":\"adc_res\",\"v\":%.3f}\n", voltage);
        }
        HAL_ADC_Stop(&hadc1);
    }
}

// Timer callback function
static void GPIO_Toggle_Callback(void *argument) {
    const PinMap_t* pInfo = (const PinMap_t*)argument;
    HAL_GPIO_TogglePin(pInfo->port, pInfo->pin);
}

void EXT_GPIO_Toggle(const char* pinName, uint32_t freq_hz) {
    int pinIdx = GetPinIndex(pinName);
    if (pinIdx < 0) { USB_Printf("ERR: Unknown pin %s\n", pinName); return; }

    const PinMap_t* pInfo = &mcuPins[pinIdx];
    ToggleTimer_t* slot = &toggle_timers[pinIdx];

    // Stop existing timer if running
    if (slot->timer) {
        osTimerStop(slot->timer);
        osTimerDelete(slot->timer);
        slot->timer = NULL;
    }

    if (freq_hz == 0) return; // Allow freq=0 to safely stop the timer

    // Force pin to output if it isn't already!
    if (active_pin_modes[pinIdx] != PINMUX_MODE_OUTPUT) {
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = pInfo->pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(pInfo->port, &GPIO_InitStruct);
        active_pin_modes[pinIdx] = PINMUX_MODE_OUTPUT;
    }

    uint32_t interval = 1000 / (2 * freq_hz);
    if (interval < 5) interval = 5; // Safety cap to prevent RTOS starvation

    slot->pin = pInfo;
    slot->timer = osTimerNew(GPIO_Toggle_Callback, osTimerPeriodic, (void*)pInfo, NULL);
    osTimerStart(slot->timer, interval);

    USB_Printf("LOG: %s toggling at %lu Hz\n", pinName, freq_hz);
}

// Add this helper function for the SPI transaction
static void AD9837_WriteReg(uint16_t data) {
    extern SPI_HandleTypeDef hspi1;
    uint8_t tx_buf[2];
    tx_buf[0] = (data >> 8) & 0xFF;
    tx_buf[1] = data & 0xFF;

    // We will use PB0 as the FSYNC (Chip Select)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, tx_buf, 2, 10);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
}

void EXT_DAC_Stop(void) {
    extern DAC_HandleTypeDef hdac1;
    // Stop any active DMA/Timer driven waveforms
    HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_2);
    HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2);
    if (htim_dac.Instance) {
        HAL_TIM_Base_Stop(&htim_dac);
    }
}

void EXT_AD9837_Stop(void) {
    // 0x21C0 asserts RESET, disables the internal MCLK (SLEEP1), and powers down the DAC (SLEEP12)
    AD9837_WriteReg(0x21C0);
}

void EXT_DAC_Set(float voltage_V) {
    extern DAC_HandleTypeDef hdac1;

    // 1. Ensure any active DMA streams are halted
    HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_2);
    if (htim_dac.Instance) HAL_TIM_Base_Stop(&htim_dac);

    // 2. Reconfigure DAC for static software control
    DAC_ChannelConfTypeDef sConfig = {0};
    sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
    HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2);

    HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);

    if (voltage_V < 0.0f) voltage_V = 0.0f;
    if (voltage_V > 3.3f) voltage_V = 3.3f;

    uint32_t dac_val = (uint32_t)((voltage_V / 3.3f) * 4095.0f);
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, dac_val);
}

void EXT_DAC_Wave(uint8_t waveform, uint32_t freq_Hz, float offset_V, float amplitude_V, float vref_V) {
    extern DAC_HandleTypeDef hdac1;

    // 1. Stop existing processes
    EXT_DAC_Stop();
    if (htim_dac.Instance) HAL_TIM_Base_DeInit(&htim_dac);
    if (hdma_dac.Instance) HAL_DMA_DeInit(&hdma_dac);

    // 2. Generate Waveform LUT Dynamically
    if (vref_V <= 0.1f) vref_V = 3.3f; // Safety against divide-by-zero

    float center_f = (offset_V / vref_V) * 4095.0f;
    float amp_f = (amplitude_V / vref_V) * 4095.0f;

    uint32_t samples = (freq_Hz >= 100000) ? 16 : 32; // Lower samples at 100kHz to save bus bandwidth

    for (int i = 0; i < samples; i++) {
        float val_f = center_f;

        if (waveform == 1) { // SINE
            val_f = center_f + (amp_f * sinf(2.0f * 3.14159265f * (float)i / (float)samples));
        } else if (waveform == 2) { // TRIANGLE
            if (i < samples / 2) {
                val_f = (center_f - amp_f) + (2.0f * amp_f * i / (samples / 2.0f));
            } else {
                val_f = (center_f + amp_f) - (2.0f * amp_f * (i - samples / 2.0f) / (samples / 2.0f));
            }
        } else if (waveform == 3) { // SQUARE
            val_f = (i < samples / 2) ? (center_f + amp_f) : (center_f - amp_f);
        }

        // Clamp values safely inside 12-bit DAC limits
        if (val_f > 4095.0f) val_f = 4095.0f;
        if (val_f < 0.0f) val_f = 0.0f;

        dac_wave_lut[i] = (uint32_t)val_f;
    }

    // 3. Setup TIM2 for DAC Hardware Triggering
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim_dac.Instance = TIM2;
    uint32_t timer_freq = freq_Hz * samples;
    htim_dac.Init.Prescaler = 0;
    htim_dac.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_dac.Init.Period = (64000000 / timer_freq) - 1; // 64MHz system clock
    htim_dac.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&htim_dac);

    TIM_MasterConfigTypeDef sMasterConfig = {0};
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim_dac, &sMasterConfig);
    HAL_TIM_Base_Start(&htim_dac);

    // 4. Setup DAC Channel 2
    DAC_ChannelConfTypeDef sConfig = {0};
    sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger = DAC_TRIGGER_T2_TRGO; // Link to TIM2
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
    HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2);

    // 5. Setup DMA Circular Buffer (Routing through DMA1_Channel7 via DMAMUX)
    __HAL_RCC_DMA1_CLK_ENABLE();
    hdma_dac.Instance = DMA1_Channel7;
    hdma_dac.Init.Request = DMA_REQUEST_DAC1_CH2;
    hdma_dac.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_dac.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_dac.Init.MemInc = DMA_MINC_ENABLE;
    hdma_dac.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_dac.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_dac.Init.Mode = DMA_CIRCULAR;
    hdma_dac.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_dac);

    __HAL_LINKDMA(&hdac1, DMA_Handle2, hdma_dac);

    // 6. Start DMA DAC Transfer
    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t*)dac_wave_lut, samples, DAC_ALIGN_12B_R);
}
void EXT_AD9837_Set(uint8_t waveform, uint32_t freq_Hz) {
    // Formula: FREQ_REG = (Fout * 2^28) / MCLK (8,000,000 Hz)
    float f_word = ((float)freq_Hz * 268435456.0f) / 8000000.0f;
    uint32_t freq_reg = (uint32_t)(f_word + 0.5f); // Round to nearest integer

    if (freq_reg > 0x0FFFFFFF) freq_reg = 0x0FFFFFFF;

    // 1. Assert Reset + B28
    AD9837_WriteReg(0x2100);

    // 2. Write 14 LSBs, then 14 MSBs
    AD9837_WriteReg(0x4000 | (freq_reg & 0x3FFF));
    AD9837_WriteReg(0x4000 | ((freq_reg >> 14) & 0x3FFF));

    // 3. Write Phase 0
    AD9837_WriteReg(0xC000);

    // 4. Configure Output Mode & Exit Reset
    uint16_t control_reg = 0x2000;
    if (waveform == 1) {
        control_reg |= 0x0002; // Triangle
    } else if (waveform == 2) {
        control_reg |= 0x0028; // Square (MSB/2)
    }

    AD9837_WriteReg(control_reg);
}


// Core -> Src -> pinmux.c

void EXT_DAC081_Set(float voltage) {
    extern I2C_HandleTypeDef hi2c2;
    const uint8_t DAC_ADDR = 0x0D << 1;

    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > 3.0f) voltage = 3.0f;
    uint8_t code = (uint8_t)((voltage / 3.0f) * 255.0f);

    uint8_t tx_buf[2];
    tx_buf[0] = (code >> 4) & 0x0F;
    tx_buf[1] = (code << 4) & 0xF0;

    // --- FIX: Clear any bus glitches before transmitting ---
    HAL_I2C_DeInit(&hi2c2);
    HAL_I2C_Init(&hi2c2);

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(&hi2c2, DAC_ADDR, tx_buf, 2, 100);

    if (status == HAL_OK) {
        USB_Printf("LOG: I2C2 DAC Write SUCCESS. Voltage set to %.2fV\n", voltage);
    } else {
        USB_Printf("ERR: I2C2 DAC Write NACK! HAL Code: %d\n", (int)status);
    }
}

// Core -> Src -> pinmux.c

void EXT_I2C_Scanner(uint8_t bus_num) {
    extern I2C_HandleTypeDef hi2c1;
    extern I2C_HandleTypeDef hi2c2;
    I2C_HandleTypeDef *hi2c = (bus_num == 1) ? &hi2c1 : &hi2c2;
    uint8_t found_any = 0;

    USB_Printf(">>> Scanning I2C%d Bus...\n", bus_num);

    // --- FIX: Force reset the I2C hardware to clear pin-muxing glitches ---
    HAL_I2C_DeInit(hi2c);
    HAL_I2C_Init(hi2c);
    osDelay(5); // Give the physical bus a moment to pull HIGH

    for (uint16_t i = 1; i < 128; i++) {
        if (HAL_I2C_IsDeviceReady(hi2c, (i << 1), 3, 2) == HAL_OK) {
            USB_Printf("FOUND: Device at 0x%02X\n", i);
            found_any = 1;
        }
    }

    if (!found_any) {
        USB_Printf("LOG: I2C%d bus is empty. (Check wiring/pull-ups)\n", bus_num);
    } else {
        USB_Printf("LOG: I2C%d scan complete.\n", bus_num);
    }
}
