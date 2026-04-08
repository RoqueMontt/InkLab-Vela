/**
 * @file error_manager.h
 * @brief Global system error handling and severity routing.
 * 
 * Law 1 Compliant: Contains absolutely no Zephyr/RTOS headers.
 */
#ifndef ERROR_MANAGER_H
#define ERROR_MANAGER_H

#include <stdint.h>

/**
 * @brief Severity defines how the MCU reacts to a fault.
 */
typedef enum {
    SEV_INFO = 0,     /**< Log it via telemetry. */
    SEV_WARNING,      /**< Log it and potentially trigger a UI alert. */
    SEV_CRITICAL,     /**< Log it, abort current operation, flash Red LED. */
    SEV_FATAL         /**< Hard stop. Lock IRQs, disable rails, system panic. */
} ErrorSeverity_t;

/**
 * @brief Dictionary of known system faults.
 */
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

/**
 * @brief Elevates a system error to the appropriate handler.
 * 
 * @param code The enumerated error code.
 * @param severity The operational impact of the error.
 * @param details A human-readable string detailing the fault.
 */
void Error_Raise(ErrorCode_t code, ErrorSeverity_t severity, const char* details);

#endif /* ERROR_MANAGER_H */