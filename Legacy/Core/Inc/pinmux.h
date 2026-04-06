#ifndef PINMUX_H_
#define PINMUX_H_

#include "stm32g0xx_hal.h"
#include <stdbool.h>
#include <math.h>

// Initialize the Pinmux system (loads config from SD card if available)
void Pinmux_Init(void);

// Apply a specific function to an MCU pin (e.g. "PA5", "USART3_TX")
HAL_StatusTypeDef Pinmux_SetPin(const char* pinName, const char* funcName);

// Save the current routing configuration to the SD card
void Pinmux_SaveConfig(void);

// --- Agnostic External Interface Commands ---
void EXT_UART_Tx(uint8_t uart_num, const char* payload);
void EXT_I2C_Write(uint8_t i2c_num, uint8_t addr, uint8_t data);
void EXT_GPIO_Write(const char* pinName, uint8_t state);
void EXT_GPIO_Toggle(const char* pinName, uint32_t freq_hz);
void EXT_ADC_Read(const char* pinName);
void Pinmux_GetInputTelemetryJson(char* buffer, size_t max_len);
void EXT_GPIO_Toggle(const char* pinName, uint32_t freq_hz);

void EXT_DAC_Set(float voltage_V);
void EXT_AD9837_Set(uint8_t waveform, uint32_t freq_Hz);
void EXT_DAC_Stop(void);
void EXT_AD9837_Stop(void);
void EXT_DAC081_Set(float voltage);
#endif /* PINMUX_H_ */
