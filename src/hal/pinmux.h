/**
 * @file pinmux.h
 * @brief Dynamic runtime hardware routing mappings for external pins.
 * 
 * Law 1 Compliant: Contains absolutely no Zephyr/RTOS headers.
 */
#ifndef PINMUX_H
#define PINMUX_H

#include <stdint.h>

/**
 * @brief Initializes the dynamic pinmux subsystem from non-volatile storage.
 * 
 * Reads the saved configuration (e.g., from the SD card) and uses Zephyr's 
 * dynamic pinctrl to apply the routing before the system starts.
 * 
 * @return 0 on success, negative errno on filesystem or parsing failure.
 */
int Pinmux_Init(void);

/**
 * @brief Stages a dynamic routing request for a specific pin.
 * 
 * Records the user's intent to route a pin. The change is not applied to 
 * hardware immediately. It must be finalized with Pinmux_Apply().
 * 
 * @param pinName String identifier for the MCU physical pin (e.g., "PA5").
 * @param funcName String identifier for the peripheral function (e.g., "USART3_TX").
 * @return 0 on success, -EINVAL if the parameters are invalid.
 */
int Pinmux_SetPin(const char* pinName, const char* funcName);

/**
 * @brief Commits staged pinmux configurations to storage and reboots.
 * 
 * Saves the current routing map to the SD card and triggers a soft MCU reset
 * (SYS_REBOOT_COLD) to apply the hardware changes atomically on boot.
 * 
 * @return 0 on success, negative errno on write failure.
 */
int Pinmux_Apply(void);

#endif /* PINMUX_H */