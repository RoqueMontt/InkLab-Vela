#ifndef BATTERY_SOC_H
#define BATTERY_SOC_H

#include <stdint.h>

/**
 * @brief Initialize the SOC tracking algorithm
 */
void BatterySOC_Init(void);

/**
 * @brief Calculates the State of Charge (%) and Internal Resistance (Ohms)
 * @param vbat_V Current battery voltage in Volts
 * @param current_mA Current flowing into/out of battery in mA
 * @param out_soc Pointer to store the calculated SOC %
 * @param out_r_int Pointer to store the calculated Internal Resistance
 */
void BatterySOC_Update(float vbat_V, float current_mA, float* out_soc, float* out_r_int);

#endif // BATTERY_SOC_H
