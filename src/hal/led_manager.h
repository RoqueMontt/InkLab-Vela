/**
 * @file led_manager.h
 * @brief RGB LED manager for InkLab-Vela.
 *
 * Provides a simple software-PWM heartbeat and FPGA-ready indicator driven
 * from a periodic timer callback (LED_PWM_Tick).  All GPIO access is done
 * via the Zephyr GPIO DT API.
 *
 * The overlay declares:
 *   alias led0 = &led_r  (GPIOC 15, ACTIVE_LOW)
 *   alias led1 = &led_g  (GPIOC 14, ACTIVE_LOW)
 *   alias led2 = &led_b  (GPIOC 13, ACTIVE_LOW)
 */

#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Initialisation
 * -------------------------------------------------------------------------- */

/**
 * @brief Bind GPIO specs and drive all LEDs off.
 *        Must be called once before any other LED function.
 * @return 0 on success, negative errno on GPIO init failure.
 */
int LED_Init(void);

/* --------------------------------------------------------------------------
 * State setters (thread-safe — simple atomic flags)
 * -------------------------------------------------------------------------- */

/**
 * @brief Set per-channel brightness limits (0–100 %).
 *        Passing a new limit automatically clears any mute.
 */
void LED_SetLimits(uint8_t r_percent, uint8_t g_percent, uint8_t b_percent);

/** @brief Mute (all off) or un-mute the LED outputs. */
void LED_SetMute(bool mute);

/** @brief Toggle the heartbeat state (call every ~500 ms from LED task). */
void LED_ToggleHeartbeat(void);

/** @brief Reflect the FPGA programming result on the status LED. */
void LED_SetFpgaReady(bool is_ready);

/**
 * @brief Force all channels on at their configured limits (diagnostic mode).
 *        Setting override=false returns to the normal heartbeat/status logic.
 */
void LED_SetOverride(bool override);

/* --------------------------------------------------------------------------
 * PWM tick  (call from a 100 Hz periodic context)
 * -------------------------------------------------------------------------- */

/**
 * @brief Software-PWM tick — must be called at a stable 100 Hz rate.
 *
 * Drives the three GPIO lines according to the current state.  Safe to call
 * from a k_timer callback or a dedicated low-priority thread.
 */
void LED_PWM_Tick(void);

/* --------------------------------------------------------------------------
 * Telemetry helper
 * -------------------------------------------------------------------------- */

/** @brief Read back current brightness limits and mute flag. */
void LED_GetStatus(uint8_t *r, uint8_t *g, uint8_t *b, bool *muted);

#endif /* LED_MANAGER_H */