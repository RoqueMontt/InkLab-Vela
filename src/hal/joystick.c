/**
 * @file joystick.c
 * @brief 5-way joystick driver — Zephyr port.
 *
 * Key differences from the STM32/HAL version:
 *  - Switch pins are read via gpio_pin_get_dt() using DT aliases.
 *  - The ACTIVE_LOW flag in the overlay handles the active-low inversion,
 *    so gpio_pin_get_dt() returns 1 when the button IS pressed.
 *  - USB_Printf() is replaced by printk() (routed to CDC-ACM via Zephyr console).
 */

#include "hal/joystick.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* --------------------------------------------------------------------------
 * Device-tree GPIO specs  (aliases defined in inklab_v1.overlay)
 *
 *   joy-up    → gpiod 1  (SW_B, ACTIVE_LOW | PULL_UP)
 *   joy-down  → gpiob 8  (SW_C, ACTIVE_LOW | PULL_UP)
 *   joy-left  → gpioc 7  (SW_A, ACTIVE_LOW | PULL_UP)
 *   joy-right → gpiob 9  (SW_D, ACTIVE_LOW | PULL_UP)
 *   joy-center→ gpiod 2  (SW_CEN, ACTIVE_LOW | PULL_UP)
 * -------------------------------------------------------------------------- */
static const struct gpio_dt_spec joy_up    = GPIO_DT_SPEC_GET(DT_ALIAS(joy_up),     gpios);
static const struct gpio_dt_spec joy_down  = GPIO_DT_SPEC_GET(DT_ALIAS(joy_down),   gpios);
static const struct gpio_dt_spec joy_left  = GPIO_DT_SPEC_GET(DT_ALIAS(joy_left),   gpios);
static const struct gpio_dt_spec joy_right = GPIO_DT_SPEC_GET(DT_ALIAS(joy_right),  gpios);
static const struct gpio_dt_spec joy_cen   = GPIO_DT_SPEC_GET(DT_ALIAS(joy_center), gpios);

/* --------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------- */
static uint8_t      prev_state   = 0xFF;  /* Tracks last combined pin state */
static uint8_t      last_up      = 0;
static uint8_t      last_down    = 0;
static uint8_t      last_left    = 0;
static uint8_t      last_right   = 0;
static uint8_t      last_cen     = 0;

static JoystickMap_t current_map = { .ud = 0, .lr = 0, .cen = 0 };

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int Joystick_Init(void)
{
    const struct gpio_dt_spec *pins[] = {
        &joy_up, &joy_down, &joy_left, &joy_right, &joy_cen
    };

    for (int i = 0; i < 5; i++) {
        if (!gpio_is_ready_dt(pins[i])) {
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(pins[i], GPIO_INPUT);
        if (ret < 0) {
            return ret;
        }
    }

    prev_state = 0xFF; /* Force a state update on first Process() call */
    return 0;
}

void Joystick_SetMap(uint8_t ud, uint8_t lr, uint8_t cen)
{
    current_map.ud  = ud;
    current_map.lr  = lr;
    current_map.cen = cen;
}

JoystickMap_t Joystick_GetMap(void)
{
    return current_map;
}

JoyAction_t Joystick_Process(void)
{
    /*
     * gpio_pin_get_dt() returns 1 when the button IS pressed (ACTIVE_LOW is
     * already resolved by the GPIO layer).  We encode all five into one byte
     * so a single comparison detects any change.
     */
    uint8_t up    = (uint8_t)gpio_pin_get_dt(&joy_up);
    uint8_t down  = (uint8_t)gpio_pin_get_dt(&joy_down);
    uint8_t left  = (uint8_t)gpio_pin_get_dt(&joy_left);
    uint8_t right = (uint8_t)gpio_pin_get_dt(&joy_right);
    uint8_t cen   = (uint8_t)gpio_pin_get_dt(&joy_cen);

    uint8_t current_state = (up << 4) | (down << 3) | (left << 2) | (right << 1) | cen;
    JoyAction_t triggered = JOY_NONE;

    if (current_state != prev_state) {
        /* Report raw switch state to the UI dashboard */
        printk("{\"type\":\"io\",\"u\":%d,\"d\":%d,\"l\":%d,\"r\":%d,\"c\":%d}\n",
               up, down, left, right, cen);

        /* Detect falling-edge triggers (button newly pressed) */
        if      (up    && !last_up)    { triggered = JOY_UP;     }
        else if (down  && !last_down)  { triggered = JOY_DOWN;   }
        else if (left  && !last_left)  { triggered = JOY_LEFT;   }
        else if (right && !last_right) { triggered = JOY_RIGHT;  }
        else if (cen   && !last_cen)   { triggered = JOY_CENTER; }

        prev_state = current_state;
    }

    last_up    = up;
    last_down  = down;
    last_left  = left;
    last_right = right;
    last_cen   = cen;

    return triggered;
}