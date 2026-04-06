#include "led_manager.h"
#include "main.h"

static uint8_t led_limit_r = 25;
static uint8_t led_limit_g = 10;
static uint8_t led_limit_b = 10;

static volatile bool led_muted = false;
static volatile bool heartbeat_state = false;
static volatile bool fpga_ready = false;
static volatile bool led_override = false;



void LED_Init(void) {
    // Ensure pins start high (off for common anode)
    HAL_GPIO_WritePin(GPIOC, LED_RGB2_Pin | LED_RGB1_Pin | LED_RGB0_Pin, GPIO_PIN_SET);
}

void LED_SetLimits(uint8_t r_percent, uint8_t g_percent, uint8_t b_percent) {
    led_limit_r = r_percent;
    led_limit_g = g_percent;
    led_limit_b = b_percent;
    led_muted = false; // Interacting with sliders unmutes automatically
}

void LED_SetMute(bool mute) {
    led_muted = mute;
    if (mute) {
        HAL_GPIO_WritePin(GPIOC, LED_RGB2_Pin | LED_RGB1_Pin | LED_RGB0_Pin, GPIO_PIN_SET);
    }
}

void LED_ToggleHeartbeat(void) {
    heartbeat_state = !heartbeat_state;
}

void LED_SetFpgaReady(bool is_ready) {
    fpga_ready = is_ready;
}

void LED_SetOverride(bool override) {
    led_override = override;
}

// Called directly by the Timer Interrupt in main.c
void LED_PWM_Tick(void) {
    static uint8_t pwm_cnt = 0;
    if (++pwm_cnt >= 100) pwm_cnt = 0;

    if (led_muted) return; // Prevent toggling if sleeping/muted

    // If override is active, force 'wants_on' to true so PWM sweeps work!
    uint8_t red_wants_on   = led_override ? 1 : !fpga_ready;
    uint8_t green_wants_on = led_override ? 1 : fpga_ready;
    uint8_t blue_wants_on  = led_override ? 1 : heartbeat_state;

    // Common Anode logic (RESET = ON, SET = OFF)
    HAL_GPIO_WritePin(GPIOC, LED_RGB0_Pin, (red_wants_on   && pwm_cnt < led_limit_r) ? GPIO_PIN_RESET : GPIO_PIN_SET); // RED
    HAL_GPIO_WritePin(GPIOC, LED_RGB1_Pin, (blue_wants_on  && pwm_cnt < led_limit_b) ? GPIO_PIN_RESET : GPIO_PIN_SET); // BLUE
    HAL_GPIO_WritePin(GPIOC, LED_RGB2_Pin, (green_wants_on && pwm_cnt < led_limit_g) ? GPIO_PIN_RESET : GPIO_PIN_SET); // GREEN
}

void LED_GetStatus(uint8_t* r, uint8_t* g, uint8_t* b, bool* muted) {
    *r = led_limit_r;
    *g = led_limit_g;
    *b = led_limit_b;
    *muted = led_muted;
}
