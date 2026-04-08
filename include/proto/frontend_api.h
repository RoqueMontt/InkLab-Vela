/**
 * @file frontend_api.h
 * @brief Frontend API parser and command handler.
 * 
 * Law 1 Compliant: Contains absolutely no Zephyr/RTOS headers.
 */
#ifndef FRONTEND_API_H
#define FRONTEND_API_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief One-time module initialisation for the frontend API state machine.
 */
void Frontend_Init(void);

/**
 * @brief Process a raw USB RX block and route parsed commands.
 *
 * @param data Pointer to received byte buffer.
 * @param len  Number of valid bytes in the buffer.
 */
void Frontend_ProcessBlock(const uint8_t *data, uint16_t len);

/**
 * @brief Upload watchdog to abort stalled binary uploads.
 *        Should be called periodically from the handling thread.
 */
void Frontend_CheckTimeout(void);

/**
 * @brief Parse and dispatch a single null-terminated command string.
 * @param cmd_str Formatted string payload.
 */
void Frontend_ExecuteCommand(char *cmd_str);

#endif /* FRONTEND_API_H */