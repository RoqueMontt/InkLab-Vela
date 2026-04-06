#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdint.h>
#include <stdbool.h>

// User-configurable mappings for UI dropdowns
typedef struct {
    uint8_t ud;  // Up/Down Mapping
    uint8_t lr;  // Left/Right Mapping
    uint8_t cen; // Center Mapping
} JoystickMap_t;

// Enum for buttons that were just pressed
typedef enum {
    JOY_NONE = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
    JOY_CENTER
} JoyAction_t;

void Joystick_Init(void);
void Joystick_SetMap(uint8_t ud, uint8_t lr, uint8_t cen);
JoystickMap_t Joystick_GetMap(void);

/**
 * @brief Reads the hardware, transmits JSON status, and returns the button pressed
 * @return JoyAction_t representing the edge-triggered action
 */
JoyAction_t Joystick_Process(void);

#endif // JOYSTICK_H
