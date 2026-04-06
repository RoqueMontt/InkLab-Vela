#include "battery_defs.h"

// Standard 3.7V/4.2V Li-ion cell (e.g., typical 18650)
const BatteryProfile_t BATT_PROFILE_LIION_3200 = {
    .type = BATT_TYPE_LIION,
    .capacity_mAh = 1500,
    .charge_voltage_mV = 4200,       // 4.2V
    .min_system_voltage_mV = 3300,   // 3.5V
    .fast_charge_current_mA = 500,  // 1.5A
    .precharge_current_mA = 120,     // 120mA
    .term_current_mA = 160           // 160mA
};

// Standard 3.2V/3.65V LiFePO4 cell
const BatteryProfile_t BATT_PROFILE_LFP_2500 = {
    .type = BATT_TYPE_LFP,
    .capacity_mAh = 2500,
    .charge_voltage_mV = 3650,       // 3.65V
    .min_system_voltage_mV = 3000,   // 3.0V
    .fast_charge_current_mA = 500,  // 1.5A
    .precharge_current_mA = 120,
    .term_current_mA = 120
};

// Standard Sodium-Ion cell (typically 3.9V to 4.1V max depending on exact chemistry)
const BatteryProfile_t BATT_PROFILE_NAION_2000 = {
    .type = BATT_TYPE_NAION,
    .capacity_mAh = 2000,
    .charge_voltage_mV = 4000,       // 4.0V max
    .min_system_voltage_mV = 2500,   // 2.5V min
    .fast_charge_current_mA = 500,  // 1.0A
    .precharge_current_mA = 120,
    .term_current_mA = 100
};
