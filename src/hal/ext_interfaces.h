/**
 * @file ext_interfaces.h
 * @brief Unified interface for generic user-commanded hardware accesses.
 * 
 * Law 1 Compliant: Contains absolutely no Zephyr/RTOS headers.
 */
#ifndef EXT_INTERFACES_H
#define EXT_INTERFACES_H

#include <stdint.h>

/**
 * @brief Sends an arbitrary payload out of the specified UART.
 * 
 * @param uart_num The target UART index (e.g., 3 for USART3).
 * @param payload Null-terminated string to transmit.
 */
void EXT_UART_Tx(uint8_t uart_num, const char* payload);

/**
 * @brief Directly writes a byte over a specific I2C bus.
 * 
 * @param i2c_num The target I2C bus index (e.g., 1 or 2).
 * @param addr The 7-bit I2C device address.
 * @param data The byte payload to transmit.
 */
void EXT_I2C_Write(uint8_t i2c_num, uint8_t addr, uint8_t data);

/**
 * @brief Directly writes a logical state to a specific GPIO pin.
 * 
 * @param pinName String identifier for the pin (e.g., "PB11").
 * @param state 1 for HIGH, 0 for LOW.
 */
void EXT_GPIO_Write(const char* pinName, uint8_t state);

/**
 * @brief Configures a periodic hardware timer to toggle a GPIO pin.
 * 
 * @param pinName String identifier for the pin (e.g., "PB11").
 * @param freq_hz Target toggle frequency in Hertz. Set to 0 to stop.
 */
void EXT_GPIO_Toggle(const char* pinName, uint32_t freq_hz);

/**
 * @brief Executes a software-triggered ADC conversion.
 * 
 * @param pinName String identifier of the target ADC channel (e.g., "AIN9").
 */
void EXT_ADC_Read(const char* pinName);

/**
 * @brief Sets a static DC voltage output on the integrated DAC.
 * 
 * @param voltage_V Target voltage in Volts.
 */
void EXT_DAC_Set(float voltage_V);

/**
 * @brief Generates a waveform on the DAC via continuous transfers.
 * 
 * @param waveform Integer ID of the wave shape (1=Sine, 2=Triangle, 3=Square).
 * @param freq_Hz Target fundamental frequency of the wave.
 * @param offset_V DC offset in Volts.
 * @param amplitude_V Peak-to-Peak amplitude in Volts.
 * @param vref_V Reference voltage scaling factor.
 */
void EXT_DAC_Wave(uint8_t waveform, uint32_t freq_Hz, float offset_V, float amplitude_V, float vref_V);

/**
 * @brief Stops all active integrated DAC outputs.
 */
void EXT_DAC_Stop(void);

/**
 * @brief Configures an external AD9837 waveform generator over SPI.
 * 
 * @param waveform Integer ID of the wave shape.
 * @param freq_Hz Target output frequency in Hertz.
 */
void EXT_AD9837_Set(uint8_t waveform, uint32_t freq_Hz);

/**
 * @brief Issues the hardware shutdown command to the external AD9837.
 */
void EXT_AD9837_Stop(void);

/**
 * @brief Sets a DC voltage on an external I2C-based DAC081.
 * 
 * @param voltage Target voltage in Volts.
 */
void EXT_DAC081_Set(float voltage);

/**
 * @brief Scans an I2C bus and outputs all responsive device addresses to USB.
 * 
 * @param bus_num The target I2C bus index (1 or 2).
 */
void EXT_I2C_Scanner(uint8_t bus_num);

#endif /* EXT_INTERFACES_H */