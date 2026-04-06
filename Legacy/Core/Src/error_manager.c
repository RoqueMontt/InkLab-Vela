#include "error_manager.h"
#include "main.h"
#include "led_manager.h"
#include "cmsis_os.h"
#include <stdio.h>

// External reference to your USB logger
extern void USB_Printf(const char *format, ...);

void Error_Raise(ErrorCode_t code, ErrorSeverity_t severity, const char* details) {
    // 1. Broadcast the structured error to the Vue.js Frontend
    USB_Printf("{\"type\":\"sys_err\",\"code\":%d,\"sev\":%d,\"msg\":\"%s\"}\n",
               code, severity, details ? details : "Unknown");

    // 2. Hardware-level reaction based on severity
    if (severity == SEV_FATAL) {
        // Disable interrupts so FreeRTOS stops switching tasks
        __disable_irq();

        // Optional: Put specific GPIO pins in safe states here
        // e.g., HAL_GPIO_WritePin(PWR_EN_TPS65_GPIO_Port, PWR_EN_TPS65_Pin, GPIO_PIN_RESET);

        // Force the LED override on
        LED_SetOverride(true);

        // Infinite death loop: Flash the Red LED permanently
        while (1) {
            HAL_GPIO_TogglePin(GPIOC, LED_RGB0_Pin); // Toggle Red
            for(volatile uint32_t i = 0; i < 2000000; i++); // Dumb delay
        }
    }
    else if (severity == SEV_CRITICAL) {
        // For critical errors, we don't halt the OS, but we force the red LED on
        // to visually alert the user that a major operation failed.
        LED_SetOverride(true);
        HAL_GPIO_WritePin(GPIOC, LED_RGB0_Pin, GPIO_PIN_RESET); // Red ON
        HAL_GPIO_WritePin(GPIOC, LED_RGB1_Pin, GPIO_PIN_SET);   // Blue OFF
        HAL_GPIO_WritePin(GPIOC, LED_RGB2_Pin, GPIO_PIN_SET);   // Green OFF

        // Let it stay red for 2 seconds, then return to normal RTOS control
        osDelay(2000);
        LED_SetOverride(false);
    }
}
