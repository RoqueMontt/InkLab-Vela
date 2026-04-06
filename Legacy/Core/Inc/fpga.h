/**
 * @file    fpga.h
 * @brief   FPGA Programming Library for interfacing STM32G0B1 with ICE40UP5K
 */

#ifndef FPGA_H_
#define FPGA_H_

#include "stm32g0xx_hal.h"
#include <stdint.h>

#define FPGA_SYNC_BYTE   0x5A
typedef struct {
    uint8_t  sync;
    uint8_t  cmd;
    uint16_t length;
    uint8_t  payload[64];
    uint8_t  crc;
} FpgaSpiPacket_t;

/* Fixed bitstream length as defined */
#define FPGA_BITSTREAM_LENGTH 104090

/**
 * @brief Programs the FPGA with the provided bitstream array.
 * @param slot: 0 for internal array, 1-15 for SD card.
 * @return HAL_OK if successful, HAL_ERROR if SPI fails or nDONE stays low.
 */
HAL_StatusTypeDef FPGA_Program_Slot(uint8_t slot);
void FPGA_PowerDown(void);


#endif /* FPGA_H_ */
