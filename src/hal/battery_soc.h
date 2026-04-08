/**
 * @file battery_soc.h
 * @brief State-of-Charge estimator (OCV + internal-resistance compensation).
 *
 * Pure C, no RTOS / HAL dependency.  Feed it periodic BQ25798 telemetry and
 * it returns a filtered SOC percentage and a running estimate of R_int.
 */

#ifndef BATTERY_SOC_H
#define BATTERY_SOC_H

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief Reset the SOC estimator to its initial state.
 *        Call once before the first BatterySOC_Update().
 */
void BatterySOC_Init(void);

/**
 * @brief Update the SOC estimate with fresh telemetry.
 *
 * @param vbat_V      Battery terminal voltage in Volts.
 * @param current_mA  Charge/discharge current in mA (negative = discharge).
 * @param out_soc     [out] Filtered SOC in percent (0–100).
 * @param out_r_int   [out] Estimated internal resistance in Ohms.
 */
void BatterySOC_Update(float vbat_V, float current_mA,
                       float *out_soc, float *out_r_int);

#endif /* BATTERY_SOC_H */