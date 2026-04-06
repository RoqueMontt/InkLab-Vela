#ifndef SYS_POWER_H
#define SYS_POWER_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Check if the system is currently in deep sleep mode */
bool SysPower_IsSleeping(void);

/** @brief Drops clock to 16MHz, disables LEDs, puts sensors to sleep */
void SysPower_EnterSleep(void);

/** @brief Restores 64MHz PLL, wakes sensors, re-enables active telemetry */
void SysPower_Wake(void);
void SysPower_EnterSuperSleep(uint32_t seconds);


#endif // SYS_POWER_H
