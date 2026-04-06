#ifndef ERROR_MANAGER_H_
#define ERROR_MANAGER_H_

#include <stdint.h>

// Severity defines how the MCU reacts
typedef enum {
    SEV_INFO = 0,     // Just log it
    SEV_WARNING,      // Log it, maybe blink a yellow LED
    SEV_CRITICAL,     // Log it, abort current operation (e.g., stop FPGA flash)
    SEV_FATAL         // Hard stop. Disable power rails, flash red LED, lock up.
} ErrorSeverity_t;

// Dictionary of known system faults
typedef enum {
    ERR_NONE = 0,
    ERR_SD_MOUNT_FAIL,
    ERR_SD_FILE_NOT_FOUND,
    ERR_FPGA_PROGRAM_TIMEOUT,
    ERR_FPGA_DONE_LOW,
    ERR_I2C_BUS_STUCK,
    ERR_BQ25798_FAULT,
    ERR_SPI_DMA_TIMEOUT
} ErrorCode_t;

// Public API
void Error_Raise(ErrorCode_t code, ErrorSeverity_t severity, const char* details);

#endif /* ERROR_MANAGER_H_ */
