#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

void LED_Init(void);
void LED_SetLimits(uint8_t r_percent, uint8_t g_percent, uint8_t b_percent);
void LED_SetMute(bool mute);
void LED_ToggleHeartbeat(void);
void LED_SetFpgaReady(bool is_ready);

/** @brief Call this inside the TIM6 25kHz PeriodElapsedCallback */
void LED_PWM_Tick(void);
void LED_GetStatus(uint8_t* r, uint8_t* g, uint8_t* b, bool* muted);
void LED_SetOverride(bool override);

#endif // LED_MANAGER_H
