/**
 * @file bq25798.h
 * @brief Hardware-agnostic API for the TI BQ25798 Switched-Mode Buck-Boost Charger.
 * 
 * Law 1 Compliant: Contains absolutely no Zephyr/RTOS headers.
 */
#ifndef BQ25798_H
#define BQ25798_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declaration for opaque device pointer (Law 1) */
struct device;

/**
 * @brief Core Telemetry Structure for BQ25798.
 */
typedef struct {
    float vbus_V;            /**< VBUS Input voltage (V) */
    float vbat_V;            /**< Battery terminal voltage (V) */
    float ibus_mA;           /**< Input current (mA) */
    float ibat_mA;           /**< Battery charge/discharge current (mA) */
    float power_mW;          /**< Battery instantaneous power (mW) */
    float vbus_power_mW;     /**< Input instantaneous power (mW) */
    char status_str[16];     /**< Human-readable charge state */

    uint8_t stat0;           /**< Raw STAT0 Register */
    uint8_t stat1;           /**< Raw STAT1 Register */
    uint8_t fault0;          /**< Raw FAULT0 Register */
    uint8_t fault1;          /**< Raw FAULT1 Register */
    uint8_t ctrl0;           /**< Raw CTRL0 Register */
} BQ25798_Data_t;

/**
 * @brief Initializes the IC with baseline power path settings.
 * @param i2c_bus Opaque pointer to the Zephyr I2C device.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_Init(const struct device *i2c_bus);

/**
 * @brief Bursts-reads critical telemetry registers safely using the Bus Manager.
 * @param data Output telemetry structure.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_ReadAll(BQ25798_Data_t* data);

/**
 * @brief Formats the telemetry data into a JSON string.
 * @param data Target struct holding read data.
 * @param buffer Output char buffer.
 * @param max_len Size constraint of buffer.
 */
void BQ25798_GetJson(BQ25798_Data_t* data, char* buffer, size_t max_len);

/**
 * @brief Dumps raw registers for Web UI.
 * @param buffer Output char buffer.
 * @param max_len Size constraint of buffer.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_DumpRegisters(char* buffer, size_t max_len);

/**
 * @brief Sets the fast-charge current limit.
 * @param current_mA Target current in mA.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetChargeCurrent(uint16_t current_mA);

/**
 * @brief Sets the charge termination (cutoff) current.
 * @param current_mA Target current in mA.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetTermCurrent(uint16_t current_mA);

/**
 * @brief Sets the maximum input current limit drawn from VBUS.
 * @param current_mA Target current in mA.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetInputCurrent(uint16_t current_mA);

/**
 * @brief Sets the target regulation voltage for the battery.
 * @param voltage_mV Target voltage in mV (e.g., 4200 for Li-Ion).
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetChargeVoltage(uint16_t voltage_mV);

/**
 * @brief Sets the minimum system voltage allowed before drawing from VBUS.
 * @param voltage_mV Minimum system voltage in mV.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetMinSystemVoltage(uint16_t voltage_mV);

/**
 * @brief Sets the dynamic power management input voltage limit (VINDPM).
 * @param voltage_mV Minimum allowable input voltage in mV.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetInputVoltageLimit(uint16_t voltage_mV);

/**
 * @brief Sets the OTG (On-The-Go) boost regulation voltage.
 * @param voltage_mV Target output voltage on VBUS in OTG mode.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetOTGVoltage(uint16_t voltage_mV);

/**
 * @brief Sets the maximum current limit for OTG output mode.
 * @param current_mA Target OTG output current in mA.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetOTGCurrent(uint16_t current_mA);

/**
 * @brief Adjusts the OTG voltage incrementally.
 * @param step_mv Step to apply (can be negative).
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_AdjustVOTG(int step_mv);

/**
 * @brief Enables or disables battery charging.
 * @param enable 1 to enable charging, 0 to disable.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetChargeEnable(uint8_t enable);

/**
 * @brief Puts the charger in High-Impedance (Hi-Z) mode.
 * @param enable 1 to enter Hi-Z, 0 to exit.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetHiZ(uint8_t enable);

/**
 * @brief Enables or disables OTG (On-The-Go) boost mode.
 * @param enable 1 to enable OTG, 0 to disable.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetOTG(uint8_t enable);

/**
 * @brief Puts the device in ultra-low power Ship Mode.
 * @param mode Ship mode variant configuration.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetShipMode(uint8_t mode);

/**
 * @brief Disconnects the input ACFET to simulate VBUS removal.
 * @param disconnect 1 to disconnect, 0 to reconnect.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetInputDisconnect(uint8_t disconnect);

/**
 * @brief Arms the automated 5.5V OTG backup state machine.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_EnableBackup5V5(void);

/**
 * @brief Toggles Backup Mode capability manually.
 * @param enable 1 to allow backup mode, 0 to prohibit.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetBackup(uint8_t enable);

/**
 * @brief Toggles the routing of backup power out to the ACFET.
 * @param enable 1 to route to VBUS_OUT, 0 to isolate.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetBackupACFET(uint8_t enable);

/**
 * @brief Configures if the software should automatically re-arm backup mode.
 * @param enable 1 to enable auto-rearm, 0 to disable.
 */
void BQ25798_SetAutoRearm(uint8_t enable);

/**
 * @brief Toggles the auto-rearm state flag.
 */
void BQ25798_ToggleAutoRearm(void);

/**
 * @brief Executes the backup re-arming sequence after a power recovery.
 * @param delay_ms Milliseconds to delay before re-arming.
 * @return 0 on success, negative errno on timeout or failure.
 */
int BQ25798_RearmBackup(uint16_t delay_ms);

/**
 * @brief Enables or disables Pulse Frequency Modulation (PFM) in Forward mode.
 * @param enable 1 to enable PFM, 0 to force PWM.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetPFM_FWD(uint8_t enable);

/**
 * @brief Enables or disables Pulse Frequency Modulation (PFM) in OTG mode.
 * @param enable 1 to enable PFM, 0 to force PWM.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetPFM_OTG(uint8_t enable);

/**
 * @brief Toggles the state of PFM dynamically.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_TogglePFM(void);

/**
 * @brief Enables or disables the I2C Watchdog Timer.
 * @param enable 1 to enable watchdog, 0 to disable.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetWatchdog(uint8_t enable);

/**
 * @brief Forces a re-detection of the VBUS input source.
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_ForceDetection(void);

/**
 * @brief Sets the resolution of the internal ADC.
 * @param bits Number of bits (12, 13, 14, 15).
 * @return 0 on success, negative errno on failure.
 */
int BQ25798_SetADCResolution(uint8_t bits);

/**
 * @brief Sets the filter coefficient for the Exponential Moving Average (EMA).
 * @param coeff Float between 0.1 and 1.0.
 */
void BQ25798_SetEMA(float coeff);

#endif /* BQ25798_H */