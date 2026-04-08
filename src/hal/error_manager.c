/**
 * @file error_manager.c
 * @brief Zephyr-native implementation of the error state machine.
 */
#include "hal/error_manager.h"
#include "hal/led_manager.h"
#include "rtos/usb_task.h"
#include "system_config.h"

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/reboot.h>

/**
 * @brief Processes the error according to its severity.
 */
void Error_Raise(ErrorCode_t code, ErrorSeverity_t severity, const char* details) {
    /* 1. Broadcast the structured error to the Frontend */
    USB_Printf("{\"type\":\"sys_err\",\"code\":%d,\"sev\":%d,\"msg\":\"%s\"}\n",
               code, severity, details ? details : "Unknown");

    /* 2. Hardware-level reaction based on severity */
    if (severity == SEV_FATAL) {
        /* Zephyr Rule 2: Use irq_lock() instead of __disable_irq() */
        unsigned int key = irq_lock(); 

        LED_SetOverride(true);
        /* In a fatal state, force the red LED permanently (assuming logic elsewhere handles colors) */

        /* Trigger a formal Zephyr panic/halt instead of an arbitrary while(1) */
        k_panic(); 
        
        irq_unlock(key); /* Unreachable, but good practice */
    }
    else if (severity == SEV_CRITICAL) {
        LED_SetOverride(true);
        
        /* Zephyr Rule 1: Never sleep in an ISR! Check context first. */
        if (!k_is_in_isr()) {
            /* Safe to sleep the calling thread to hold the LED state */
            k_msleep(ERR_LED_HOLD_TIME_MS);
            LED_SetOverride(false);
        } else {
            /* If raised from an ISR, we leave the override on for the main loop to handle */
            /* Or use a k_work item to clear it later. For now, exit safely. */
        }
    }
}