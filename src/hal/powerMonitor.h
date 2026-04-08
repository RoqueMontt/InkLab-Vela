/**
 * @file powerMonitor.h
 * @brief Hardware-agnostic Power Monitor Driver API.
 */
#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declaration for opaque device pointer (Law 1) */
struct device;

/**
 * @brief Programmable Gain Amplifier (PGA) Options
 */
typedef enum {
    POWER_MONITOR_PGA_40MV  = 0x0000,
    POWER_MONITOR_PGA_80MV  = 0x0800,
    POWER_MONITOR_PGA_160MV = 0x1000,
    POWER_MONITOR_PGA_320MV = 0x1800
} PowerMonitor_PGA_t;

/**
 * @brief Core Power Telemetry Data
 */
typedef struct {
    float bus_voltage_V;
    float shunt_voltage_mV;
    float current_mA;
    float power_mW;
    bool  overflow;
} PowerMonitor_Data_t;

/**
 * @brief Device handle structure for the Power Monitor
 */
typedef struct {
    const struct device *i2c_bus;
    uint8_t address;
    float r_shunt_ohms;
    float current_lsb_A;
    uint16_t cal_value;
    PowerMonitor_PGA_t current_pga;
    float current_offset_mA;
} PowerMonitor_t;

/**
 * @brief Initializes the power monitor sensor.
 * @param dev Pointer to the PowerMonitor instance.
 * @param i2c_bus Opaque pointer to the Zephyr I2C device.
 * @param addr I2C address of the sensor.
 * @param r_shunt_ohms Shunt resistor value in ohms.
 * @param max_expected_A Maximum expected current in amps.
 * @return 0 on success, negative errno on failure.
 */
int PowerMonitor_Init(PowerMonitor_t *dev, const struct device *i2c_bus, uint8_t addr, float r_shunt_ohms, float max_expected_A);

/**
 * @brief Reads voltage, current, and power from the sensor.
 * @param dev Pointer to the PowerMonitor instance.
 * @param data Pointer to the structure to hold the reading.
 * @return 0 on success, negative errno on failure.
 */
int PowerMonitor_Read(PowerMonitor_t *dev, PowerMonitor_Data_t *data);

/**
 * @brief Manually overrides the Programmable Gain Amplifier (PGA).
 * @param dev Pointer to the PowerMonitor instance.
 * @param pga PGA setting enumeration.
 * @return 0 on success, negative errno on failure.
 */
int PowerMonitor_SetPGA(PowerMonitor_t *dev, PowerMonitor_PGA_t pga);

/**
 * @brief Dynamically adjusts the PGA based on current telemetry data.
 * @param dev Pointer to the PowerMonitor instance.
 * @param current_data Pointer to the recent telemetry struct to evaluate.
 * @return 0 on success, negative errno on failure.
 */
int PowerMonitor_AutoScale(PowerMonitor_t *dev, const PowerMonitor_Data_t *current_data);

/**
 * @brief Configures the ADC for fast (10-bit) or standard (12-bit) resolution.
 * @param dev Pointer to the PowerMonitor instance.
 * @param enable 1 to enable Fast Mode, 0 for standard precision.
 * @return 0 on success, negative errno on failure.
 */
int PowerMonitor_SetFastMode(PowerMonitor_t *dev, uint8_t enable);

/**
 * @brief Toggles low-power shutdown mode.
 * @param dev Pointer to the PowerMonitor instance.
 * @param sleep 1 to enter Power-Down, 0 for continuous mode.
 * @return 0 on success, negative errno on failure.
 */
int PowerMonitor_SetSleep(PowerMonitor_t *dev, uint8_t sleep);

/**
 * @brief Triggers a single-shot conversion when in sleep mode.
 * @param dev Pointer to the PowerMonitor instance.
 * @return 0 on success, negative errno on failure.
 */
int PowerMonitor_TriggerSingleShot(PowerMonitor_t *dev);

/**
 * @brief Configures a software offset baseline to subtract from current readings.
 * @param dev Pointer to the PowerMonitor instance.
 * @param offset_mA Hardware baseline offset in mA.
 */
void PowerMonitor_SetOffset(PowerMonitor_t *dev, float offset_mA);

#endif /* POWER_MONITOR_H */