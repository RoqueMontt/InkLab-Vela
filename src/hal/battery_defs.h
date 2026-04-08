/**
 * @file battery_defs.h
 * @brief Shared battery profile type definitions.
 *
 * Hardware-agnostic.  No HAL or Zephyr headers required here — just plain C.
 * Include this wherever a BatteryProfile_t is needed (bq25798, battery_soc, …).
 */

#ifndef BATTERY_DEFS_H
#define BATTERY_DEFS_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Battery chemistry profile
 * -------------------------------------------------------------------------- */
typedef struct {
    uint16_t charge_voltage_mV;   /**< CV phase target, e.g. 4200 */
    uint16_t charge_current_mA;   /**< CC phase limit,  e.g. 1600 */
    uint16_t term_current_mA;     /**< Termination threshold,  e.g.  80 */
    uint16_t min_sys_voltage_mV;  /**< Minimum system rail,    e.g. 3000 */
} BatteryProfile_t;

/* --------------------------------------------------------------------------
 * Pre-defined profiles (defined in battery_defs.c)
 * -------------------------------------------------------------------------- */
extern const BatteryProfile_t BATT_PROFILE_LIION_3200;

#endif /* BATTERY_DEFS_H */