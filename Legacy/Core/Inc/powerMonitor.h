/**
 * @file powerMonitor.h
 * @brief Hardware-agnostic Power Monitor Driver (Optimized for RTOS)
 * @details Handles I2C communication, calibration, and power calculations
 * for power monitoring ICs. Completely decoupled
 * from application-layer formatting.
 * Running on INA219 in PCB V1.0
 */

#ifndef POWER_MONITOR_H_
#define POWER_MONITOR_H_

#include "stm32g0xx_hal.h"
#include <stdbool.h>

/* --- Standardized I2C Addresses --- */
#define POWER_MONITOR_ADDR_40  0x40
#define POWER_MONITOR_ADDR_43  0x43
#define POWER_MONITOR_ADDR_4C  0x4C
#define POWER_MONITOR_ADDR_4F  0x4F

/* --- Programmable Gain Amplifier (PGA) Options --- */
typedef enum {
    POWER_MONITOR_PGA_40MV  = 0x0000, /*!< Gain 1,  40mV Range */
    POWER_MONITOR_PGA_80MV  = 0x0800, /*!< Gain 2,  80mV Range */
    POWER_MONITOR_PGA_160MV = 0x1000, /*!< Gain 4, 160mV Range */
    POWER_MONITOR_PGA_320MV = 0x1800  /*!< Gain 8, 320mV Range */
} PowerMonitor_PGA_t;

/* --- Output Data Structure --- */
typedef struct {
    float bus_voltage_V;     /*!< Bus voltage in Volts */
    float shunt_voltage_mV;  /*!< Shunt voltage drop in millivolts */
    float current_mA;        /*!< Calculated current in milliamperes */
    float power_mW;          /*!< Calculated power in milliwatts */
    bool  overflow;          /*!< True if math/ADC overflow occurred */
} PowerMonitor_Data_t;

/* --- Device Handle Structure --- */
typedef struct {
    I2C_HandleTypeDef *hi2c;        /*!< Pointer to the I2C HAL Handle */
    uint8_t address;                /*!< 7-bit I2C address */
    float r_shunt_ohms;             /*!< Physical shunt resistor value */
    float current_lsb_A;            /*!< Calculated Current LSB */
    uint16_t cal_value;             /*!< Calculated Calibration Register value */
    PowerMonitor_PGA_t current_pga; /*!< Active PGA setting */
    float current_offset_mA;        /*!< NEW: Hardware baseline offset (Tare) */
} PowerMonitor_t;

/* --- Core Initialization & Reading --- */
HAL_StatusTypeDef PowerMonitor_Init(PowerMonitor_t *dev, I2C_HandleTypeDef *hi2c, uint8_t addr, float r_shunt_ohms, float max_expected_A);
HAL_StatusTypeDef PowerMonitor_Read(PowerMonitor_t *dev, PowerMonitor_Data_t *data);

/* --- Advanced Hardware Control --- */
HAL_StatusTypeDef PowerMonitor_SetPGA(PowerMonitor_t *dev, PowerMonitor_PGA_t pga);
HAL_StatusTypeDef PowerMonitor_AutoScale(PowerMonitor_t *dev, const PowerMonitor_Data_t *current_data);
HAL_StatusTypeDef PowerMonitor_SetFastMode(PowerMonitor_t *dev, uint8_t enable);
HAL_StatusTypeDef PowerMonitor_SetSleep(PowerMonitor_t *dev, uint8_t sleep);
HAL_StatusTypeDef PowerMonitor_TriggerSingleShot(PowerMonitor_t *dev);
void PowerMonitor_SetOffset(PowerMonitor_t *dev, float offset_mA);
#endif /* POWER_MONITOR_H_ */
