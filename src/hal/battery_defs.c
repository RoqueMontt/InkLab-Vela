/**
 * @file battery_defs.c
 * @brief Concrete battery profile instances.
 */

#include "battery_defs.h"
#include "system_config.h"


/* Standard 3200 mAh Li-Ion cell (e.g. INR18650-32E) */
const BatteryProfile_t BATT_PROFILE_LIION_3200 = {
    .charge_voltage_mV  = BATT_LIION_3200_CHG_MV,
    .charge_current_mA  = BATT_LIION_3200_CHG_MA,
    .term_current_mA    = BATT_LIION_3200_TERM_MA,
    .min_sys_voltage_mV = BATT_LIION_3200_SYS_MV,
};