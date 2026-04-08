/**
 * @file sys_power.h
 * @brief System-level power management for InkLab-Vela.
 *
 * Controls the sleep/wake transitions of the application layer:
 *  - Mutes LEDs and puts INA sensors to sleep on entry.
 *  - Wakes them up and re-enables the LED PWM timer on exit.
 *
 * Clock scaling (HSI ↔ PLL) is intentionally skipped in the Zephyr port
 * because the USB stack requires the PLL to remain active.  If you need
 * genuine low-power states use Zephyr's PM subsystem instead.
 */

#ifndef SYS_POWER_H
#define SYS_POWER_H

#include <stdbool.h>

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/** @brief Returns true if the system is currently in its low-power state. */
bool SysPower_IsSleeping(void);

/**
 * @brief Transition into the low-power state.
 *
 * Effects: LED muted, PFM enabled on BQ25798, INA sensors powered down,
 * LED PWM timer stopped.  Idempotent — safe to call when already asleep.
 */
void SysPower_EnterSleep(void);

/**
 * @brief Return to the fully-active state.
 *
 * Effects: restores INA sensors, disables PFM, un-mutes LEDs, restarts the
 * LED PWM timer.  Idempotent — safe to call when already awake.
 */
void SysPower_Wake(void);

#endif /* SYS_POWER_H */