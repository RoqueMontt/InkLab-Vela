/**
 * @file bq25798.h
 * @brief Optimized Driver for TI BQ25798 Switched-Mode Buck-Boost Charger
 * @details Handles I2C telemetry, power-path management, and backup configurations.
 */

#ifndef BQ25798_H
#define BQ25798_H

#include "main.h"
#include "battery_defs.h"
#include <stdbool.h>

#define BQ25798_I2C_ADDR (0x6B << 1)

/**
 * @brief Core Telemetry Structure
 * @note Parsed from high-speed I2C burst reads to minimize bus latency
 */
typedef struct {
    float vbus_V;
    float vbat_V;
    float ibus_mA;
    float ibat_mA;
    float power_mW;
    float vbus_power_mW;
    char status_str[16];

    // Raw Registers
    uint8_t stat0;  // REG1B: Charger Status 0 (Input states)
    uint8_t stat1;  // REG1C: Charger Status 1 (VBUS and Charge states)
    uint8_t fault0; // REG20: OVP and OCP faults
    uint8_t fault1; // REG21: SYS shorts and Thermal faults
    uint8_t ctrl0;  // REG0F: Charger control 0
} BQ25798_Data_t;

// ============================================================================
// SYSTEM INITIALIZATION & TELEMETRY
// ============================================================================
/** @brief Initialize the IC with baseline power path settings */
void BQ25798_Init(I2C_HandleTypeDef* hi2c);

/** @brief Optimized Burst-Read of all critical telemetry registers */
HAL_StatusTypeDef BQ25798_ReadAll(BQ25798_Data_t* data);

/** @brief Formats the telemetry data into a frontend-ready JSON payload */
void BQ25798_GetJson(BQ25798_Data_t* data, char* buffer, size_t max_len);

/** @brief Dumps 48 registers (0x00 to 0x2F) for the UI Debug Dashboard */
HAL_StatusTypeDef BQ25798_DumpRegisters(char* buffer, size_t max_len);

// ============================================================================
// VOLTAGE & CURRENT LIMITS
// ============================================================================
HAL_StatusTypeDef BQ25798_SetBatteryProfile(const BatteryProfile_t* profile);
void BQ25798_SetChargeCurrent(uint16_t current_mA);
void BQ25798_SetTermCurrent(uint16_t current_mA);
void BQ25798_SetInputCurrent(uint16_t current_mA);
void BQ25798_SetChargeVoltage(uint16_t voltage_mV);
void BQ25798_SetMinSystemVoltage(uint16_t voltage_mV);
void BQ25798_SetInputVoltageLimit(uint16_t voltage_mV);
void BQ25798_SetOTGVoltage(uint16_t voltage_mV);
void BQ25798_SetOTGCurrent(uint16_t current_mA);
void BQ25798_AdjustVOTG(int step_mv);

// ============================================================================
// POWER PATH & HARDWARE PROFILES
// ============================================================================
void BQ25798_SetChargeEnable(uint8_t enable);
void BQ25798_SetHiZ(uint8_t enable);
void BQ25798_SetOTG(uint8_t enable);
void BQ25798_SetShipMode(uint8_t mode);
void BQ25798_SetInputDisconnect(uint8_t disconnect);

// ============================================================================
// AUTOMATED BACKUP STATE MACHINE
// ============================================================================
void BQ25798_EnableBackup5V5(void);
void BQ25798_SetBackup(uint8_t enable);
void BQ25798_SetBackupACFET(uint8_t enable);
void BQ25798_SetAutoRearm(uint8_t enable);
void BQ25798_ToggleAutoRearm(void);
void BQ25798_RearmBackup(uint16_t delay_ms);

// ============================================================================
// ADVANCED FEATURES (PFM, ADC, Watchdog)
// ============================================================================
void BQ25798_SetPFM_FWD(uint8_t enable);
void BQ25798_SetPFM_OTG(uint8_t enable);
void BQ25798_TogglePFM(void);
void BQ25798_SetWatchdog(uint8_t enable);
void BQ25798_ForceDetection(void);
void BQ25798_SetADCResolution(uint8_t bits);
void BQ25798_SetEMA(float coeff);

#endif // BQ25798_H
