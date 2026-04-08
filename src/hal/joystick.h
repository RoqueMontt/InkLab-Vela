/**
 * @file joystick.h
 * @brief 5-way joystick driver for InkLab-Vela.
 *
 * Reads five GPIO-key aliases from the device tree and converts edge
 * transitions into a JoyAction_t event.  No hardware debounce timer is used —
 * the caller is responsible for polling at a safe rate (≥ 50 ms recommended).
 *
 * Overlay aliases:
 *   joy-up, joy-down, joy-left, joy-right, joy-center
 */

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Types
 * -------------------------------------------------------------------------- */

/** Direction actions produced by Joystick_Process(). */
typedef enum {
    JOY_NONE   = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
    JOY_CENTER,
} JoyAction_t;

/**
 * @brief User-configurable action mapping.
 *
 * Each axis field selects a "mode" (0, 1, …) that the application layer
 * interprets.  The joystick driver itself does not care about the meaning.
 */
typedef struct {
    uint8_t ud;  /**< Up/Down mode index  */
    uint8_t lr;  /**< Left/Right mode index */
    uint8_t cen; /**< Centre button mode index */
} JoystickMap_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief Bind GPIO specs and reset internal edge-detection state.
 * @return 0 on success, negative errno on GPIO init failure.
 */
int Joystick_Init(void);

/** @brief Update the axis-to-mode mapping. */
void Joystick_SetMap(uint8_t ud, uint8_t lr, uint8_t cen);

/** @brief Read the current axis-to-mode mapping. */
JoystickMap_t Joystick_GetMap(void);

/**
 * @brief Sample all buttons and return the first edge-triggered action.
 *
 * Also emits a JSON state frame via printk() whenever any pin changes, so
 * the UI dashboard can track raw switch states in real time.
 *
 * @return JOY_NONE if nothing changed since the last call, otherwise the
 *         action that was newly triggered.
 */
JoyAction_t Joystick_Process(void);

#endif /* JOYSTICK_H */