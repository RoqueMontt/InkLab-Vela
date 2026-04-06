#ifndef BATTERY_DEFS_H
#define BATTERY_DEFS_H

#include <stdint.h>

// Battery Chemistries
typedef enum {
    BATT_TYPE_LIION = 0,
    BATT_TYPE_LFP,    // LiFePO4
    BATT_TYPE_NAION   // Sodium-ion
} BatteryType_t;

// Battery Profile Structure
typedef struct {
    BatteryType_t type;
    uint16_t capacity_mAh;
    uint16_t charge_voltage_mV;      // VREG (Max Charge Voltage)
    uint16_t min_system_voltage_mV;  // VSYSMIN (Minimum voltage to keep system alive)
    uint16_t fast_charge_current_mA; // ICHG
    uint16_t precharge_current_mA;   // IPRECHG
    uint16_t term_current_mA;        // ITERM (Charge Cutoff Current)
} BatteryProfile_t;

// Pre-defined standard profiles
extern const BatteryProfile_t BATT_PROFILE_LIION_3200;
extern const BatteryProfile_t BATT_PROFILE_LFP_2500;
extern const BatteryProfile_t BATT_PROFILE_NAION_2000;

#endif // BATTERY_DEFS_H
