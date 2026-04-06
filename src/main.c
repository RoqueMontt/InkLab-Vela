#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Ask the device tree for the pin details of "led0" */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void) {
    /* Verify the GPIO hardware is ready */
    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }

    /* Configure PC15 as an output. Because of ACTIVE_LOW in the overlay,
       setting it to "ACTIVE" will pull the physical pin to 0V and turn the LED ON. */
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500); /* RTOS safe delay: 500ms */
    }
}