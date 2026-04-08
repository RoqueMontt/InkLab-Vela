/**
 * @file sys_power.c
 * @brief Zephyr-native implementation of low-power suspend states.
 */
#include "hal/sys_power.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/pm/pm.h>

#include "hal/bq25798.h"
#include "hal/powerMonitor.h"
#include "hal/led_manager.h"
#include "system_config.h"

extern PowerMonitor_t ina_1v2_core;
extern PowerMonitor_t ina_3v3_ext;
extern PowerMonitor_t ina_3v3_fpga;
extern PowerMonitor_t ina_3v3_mcu;

extern struct k_timer led_pwm_timer;

static bool is_sleeping = false;

bool SysPower_IsSleeping(void) {
    return is_sleeping;
}

void SysPower_EnterSleep(void) {
    if (is_sleeping) return;
    is_sleeping = true;

    LED_SetMute(true);
    BQ25798_SetPFM_FWD(1);
    k_timer_stop(&led_pwm_timer);

    /* Suspend external I2C monitors */
    PowerMonitor_SetSleep(&ina_1v2_core,  1);
    PowerMonitor_SetSleep(&ina_3v3_ext,   1);
    PowerMonitor_SetSleep(&ina_3v3_fpga,  1);
    PowerMonitor_SetSleep(&ina_3v3_mcu,   1);

    printk("LOG: Entering Low-Power State (Peripherals Sleep)\n");

    #ifdef CONFIG_PM
        /* Formally instruct the Zephyr kernel to suspend the CPU on idle */
        pm_state_set(0u, &(struct pm_state_info){PM_STATE_SUSPEND_TO_IDLE, 0, 0});
    #else
        printk("LOG: Zephyr Deep Sleep disabled to maintain USB clock.\n");
    #endif
}

void SysPower_Wake(void) {
    if (!is_sleeping) return;

    /* Returning to Active restores standard clocks automatically via Zephyr PM */
    BQ25798_SetPFM_FWD(0);
    k_sleep(K_MSEC(50));

    is_sleeping = false;
    LED_SetMute(false);
    k_timer_start(&led_pwm_timer, K_MSEC(LED_PWM_PERIOD_MS), K_MSEC(LED_PWM_PERIOD_MS));

    PowerMonitor_SetSleep(&ina_1v2_core,  0);
    PowerMonitor_SetSleep(&ina_3v3_ext,   0);
    PowerMonitor_SetSleep(&ina_3v3_fpga,  0);
    PowerMonitor_SetSleep(&ina_3v3_mcu,   0);

    printk("LOG: System fully awake.\n");
}