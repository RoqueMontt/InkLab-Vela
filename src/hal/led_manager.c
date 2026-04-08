/**
 * @file led_manager.c
 * @brief RGB LED manager — Zephyr port.
 *
 * Key differences from the STM32/HAL version:
 *  - GPIO access uses Zephyr's gpio_pin_set_dt() instead of HAL_GPIO_WritePin().
 *  - LED specs are fetched at init via GPIO_DT_SPEC_GET(DT_ALIAS(...)).
 *  - No HAL, no cmsis_os — pure Zephyr.
 */

#include "hal/led_manager.h"
#include "system_config.h"
#include "system_config.h" 

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* --------------------------------------------------------------------------
 * Device-tree GPIO specs  (aliases defined in inklab_v1.overlay)
 *   led0 → led_r → GPIOC 15 ACTIVE_LOW
 *   led1 → led_g → GPIOC 14 ACTIVE_LOW
 *   led2 → led_b → GPIOC 13 ACTIVE_LOW
 * -------------------------------------------------------------------------- */
static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* --------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------- */
static uint8_t  led_limit_r   = LED_DEFAULT_LIMIT_R;
static uint8_t  led_limit_g   = LED_DEFAULT_LIMIT_G;
static uint8_t  led_limit_b   = LED_DEFAULT_LIMIT_B;

static volatile bool led_muted       = false;
static volatile bool heartbeat_state = false;
static volatile bool fpga_ready      = false;
static volatile bool led_override    = false;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/** @brief Internal helper to safely drive a specific LED channel, abstracting active-low DT logic. */
static inline void set_led(const struct gpio_dt_spec *spec, bool active)
{
    /* gpio_pin_set_dt respects ACTIVE_LOW automatically */
    gpio_pin_set_dt(spec, active ? 1 : 0);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int LED_Init(void)
{
    if (!gpio_is_ready_dt(&led_r) ||
        !gpio_is_ready_dt(&led_g) ||
        !gpio_is_ready_dt(&led_b)) {
        return -ENODEV;
    }

    gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);

    return 0;
}

void LED_SetLimits(uint8_t r_percent, uint8_t g_percent, uint8_t b_percent)
{
    led_limit_r = r_percent;
    led_limit_g = g_percent;
    led_limit_b = b_percent;
    led_muted   = false; /* Interacting with sliders unmutes automatically */
}

void LED_SetMute(bool mute)
{
    led_muted = mute;
    if (mute) {
        /* Immediately extinguish all channels */
        set_led(&led_r, false);
        set_led(&led_g, false);
        set_led(&led_b, false);
    }
}

void LED_ToggleHeartbeat(void)
{
    heartbeat_state = !heartbeat_state;
}

void LED_SetFpgaReady(bool is_ready)
{
    fpga_ready = is_ready;
}

void LED_SetOverride(bool override)
{
    led_override = override;
}

void LED_PWM_Tick(void)
{
    static uint8_t pwm_cnt = 0;

    if (++pwm_cnt >= LED_PWM_MAX) { // Replace 100
        pwm_cnt = 0;
    }

    if (led_muted) {
        return;
    }

    /*
     * When override is active all three channels follow their limit (diagnostic).
     * Otherwise each channel carries a specific meaning:
     *   RED   → FPGA NOT ready
     *   GREEN → FPGA ready
     *   BLUE  → heartbeat pulse
     */
    bool red_on   = led_override ? true : !fpga_ready;
    bool green_on = led_override ? true :  fpga_ready;
    bool blue_on  = led_override ? true :  heartbeat_state;

    set_led(&led_r, red_on   && (pwm_cnt < led_limit_r));
    set_led(&led_g, green_on && (pwm_cnt < led_limit_g));
    set_led(&led_b, blue_on  && (pwm_cnt < led_limit_b));
}

void LED_GetStatus(uint8_t *r, uint8_t *g, uint8_t *b, bool *muted)
{
    *r     = led_limit_r;
    *g     = led_limit_g;
    *b     = led_limit_b;
    *muted = led_muted;
}

/* --------------------------------------------------------------------------
 * Zephyr Native Timer Integration
 * -------------------------------------------------------------------------- */
 
 /** @brief Hardware timer callback that drives the software PWM matrix. */
static void led_timer_handler(struct k_timer *timer_id)
{
    /* Call the agnostic tick handler */
    LED_PWM_Tick();
}

/* Globally define the timer so it can be referenced by sys_power.c */
K_TIMER_DEFINE(led_pwm_timer, led_timer_handler, NULL);