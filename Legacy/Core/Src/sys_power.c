#include "sys_power.h"
#include "main.h"
#include "bq25798.h"
#include "powerMonitor.h"
#include "led_manager.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

extern TIM_HandleTypeDef htim6;
extern PowerMonitor_t ina_1v2_core, ina_3v3_ext, ina_3v3_fpga, ina_3v3_stm32;

static bool is_sleeping = false;

extern void USB_Printf(const char *format, ...);

static void Enter_Low_Power_Clock(void) {
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI; // 16MHz
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
    __HAL_RCC_PLL_DISABLE();
}

static void Exit_Low_Power_Clock(void) {
    __HAL_RCC_PLL_ENABLE();
    while(__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET) {}

    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; // 64MHz
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

bool SysPower_IsSleeping(void) {
    return is_sleeping;
}

void SysPower_EnterSleep(void) {
    if (is_sleeping) return;
    is_sleeping = true;

    LED_SetMute(true);
    BQ25798_SetPFM_FWD(1);
    HAL_TIM_Base_Stop_IT(&htim6);

    // Shut down external INA sensors to save power
    PowerMonitor_SetSleep(&ina_1v2_core, 1);
    PowerMonitor_SetSleep(&ina_3v3_ext, 1);
    PowerMonitor_SetSleep(&ina_3v3_fpga, 1);
    PowerMonitor_SetSleep(&ina_3v3_stm32, 1);

    Enter_Low_Power_Clock();
    USB_Printf("LOG: Entering Deep Sleep (16MHz)\n");
}

void SysPower_Wake(void) {
    if (!is_sleeping) return;

    Exit_Low_Power_Clock();
    osDelay(10); // Stabilize clocks

    is_sleeping = false;
    LED_SetMute(false);
    BQ25798_SetPFM_FWD(0);
    osDelay(50); // Let rails stabilize

    HAL_TIM_Base_Start_IT(&htim6);

    PowerMonitor_SetSleep(&ina_1v2_core, 0);
    PowerMonitor_SetSleep(&ina_3v3_ext, 0);
    PowerMonitor_SetSleep(&ina_3v3_fpga, 0);
    PowerMonitor_SetSleep(&ina_3v3_stm32, 0);

    USB_Printf("LOG: System Awake. Restored nominal 64MHz timing.\n");
}
