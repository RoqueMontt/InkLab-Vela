/**
 * @file fpga.h
 * @brief FPGA Programming Library for SPI payload management.
 */
#ifndef FPGA_H
#define FPGA_H

#include <stdint.h>
#include "system_config.h"

/**
 * @brief Defines a formatted command packet sent to the FPGA via SPI.
 */
typedef struct {
    uint8_t  sync;          /**< Sync byte ensuring alignment */
    uint8_t  cmd;           /**< Hexadecimal command operation */
    uint16_t length;        /**< Length of trailing payload */
    uint8_t  payload[64];   /**< Data buffer */
    uint8_t  crc;           /**< Verification checksum */
} FpgaSpiPacket_t;

/**
 * @brief Reads a raw bitstream from the SD Card and flashes it to the FPGA over SPI.
 * @param slot The ID of the slot to load from the SD card.
 * @return 0 on success, negative errno on failure.
 */
int FPGA_Program_Slot(uint8_t slot);

/**
 * @brief Asserts the hardware reset lines to power-down the FPGA fabric.
 */
void FPGA_PowerDown(void);

/**
 * @brief Triggers the Zephyr event queue to power up the FPGA.
 */
void FPGA_RequestPowerUp(void);

/**
 * @brief Enqueues an SPI packet to the FPGA communication queue.
 * @param packet Pointer to the populated packet structure.
 * @return 0 on success, negative errno if the queue is full.
 */
int FPGA_EnqueueSpiPacket(const FpgaSpiPacket_t *packet);

#endif /* FPGA_H */