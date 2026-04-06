#include "joystick.h"
#include "main.h"

extern void USB_Printf(const char *format, ...);

static uint8_t prev_state = 0xFF;
static uint8_t last_sw_w = 1, last_sw_a = 1, last_sw_s = 1, last_sw_d = 1, last_sw_cen = 1;
static JoystickMap_t current_map = {0, 0, 0};

void Joystick_Init(void) {
    prev_state = 0xFF;
}

void Joystick_SetMap(uint8_t ud, uint8_t lr, uint8_t cen) {
    current_map.ud = ud;
    current_map.lr = lr;
    current_map.cen = cen;
}

JoystickMap_t Joystick_GetMap(void) {
    return current_map;
}

JoyAction_t Joystick_Process(void) {
    uint8_t sw_w = HAL_GPIO_ReadPin(GPIOB, SW_C_Pin);          // DOWN
    uint8_t sw_a = HAL_GPIO_ReadPin(SW_A_GPIO_Port, SW_A_Pin); // LEFT
    uint8_t sw_s = HAL_GPIO_ReadPin(GPIOD, SW_B_Pin);          // UP
    uint8_t sw_d = HAL_GPIO_ReadPin(GPIOB, SW_D_Pin);          // RIGHT
    uint8_t sw_c = HAL_GPIO_ReadPin(GPIOD, SW_CEN_Pin);        // CENTER

    uint8_t current_state = (sw_w << 4) | (sw_a << 3) | (sw_s << 2) | (sw_d << 1) | sw_c;
    JoyAction_t triggered_action = JOY_NONE;

    if (current_state != prev_state) {
        // Stream physical states to UI Dashboard
        USB_Printf("{\"type\":\"io\",\"w\":%d,\"a\":%d,\"s\":%d,\"d\":%d,\"c\":%d}\n", sw_w, sw_a, sw_s, sw_d, sw_c);

        // Detect Edge Triggers
        if (sw_s == 0 && last_sw_s == 1)      triggered_action = JOY_DOWN;
        else if (sw_w == 0 && last_sw_w == 1) triggered_action = JOY_UP;
        else if (sw_a == 0 && last_sw_a == 1) triggered_action = JOY_LEFT;
        else if (sw_d == 0 && last_sw_d == 1) triggered_action = JOY_RIGHT;
        else if (sw_c == 0 && last_sw_cen == 1) triggered_action = JOY_CENTER;

        prev_state = current_state;
    }

    last_sw_w = sw_w; last_sw_a = sw_a; last_sw_s = sw_s; last_sw_d = sw_d; last_sw_cen = sw_c;

    return triggered_action;
}
