/**
 * @file    fpga.c
 * @brief   FPGA Programming Library for STM32G0 optimized for FreeRTOS
 */

#include "fpga.h"
#include "main.h"
#include "ff.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>
#include "error_manager.h"

extern SPI_HandleTypeDef hspi3;
extern void USB_Printf(const char *fmt, ...);
extern volatile uint8_t sd_is_mounted;
osSemaphoreId_t FpgaDmaSemaphore = NULL;

/**
 * @brief HAL SPI Transmit Complete Callback.
 * This is triggered by the DMA interrupt when the transfer is finished.
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance == SPI3) {
        if (FpgaDmaSemaphore != NULL) {
            // Release the semaphore to wake up the FpgaTask
            osSemaphoreRelease(FpgaDmaSemaphore);
        }
    }
}

/**
 * @brief HAL SPI Transmit & Receive Complete Callback.
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    extern osSemaphoreId_t FpgaDmaSemaphore;
    if (hspi->Instance == SPI3) {
        if (FpgaDmaSemaphore != NULL) {
            osSemaphoreRelease(FpgaDmaSemaphore);
        }
    }
}

/**
 * @brief Executes the physical reset and handshake sequence for the FPGA.
 */
static void FPGA_Reset_Sequence(void) {
    HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(FPGA_RST_GPIO_Port, FPGA_RST_Pin, GPIO_PIN_RESET);
    osDelay(5);

    HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_RESET);
    osDelay(2);

    HAL_GPIO_WritePin(FPGA_RST_GPIO_Port, FPGA_RST_Pin, GPIO_PIN_SET);
    osDelay(10);
}

/**
 * @brief Programs the FPGA from either internal Flash (Slot 0) or SD Card (Slots 1-15).
 */
HAL_StatusTypeDef FPGA_Program_Slot(uint8_t slot) {
    // Initialize DMA Semaphore if it doesn't exist yet
    if (FpgaDmaSemaphore == NULL) {
        const osSemaphoreAttr_t sem_attr = { .name = "FpgaDmaSem" };
        FpgaDmaSemaphore = osSemaphoreNew(1, 0, &sem_attr);
    }

    // Ensure semaphore is completely empty before starting
    while (osSemaphoreAcquire(FpgaDmaSemaphore, 0) == osOK);

    FPGA_Reset_Sequence();

    // --- ALL SLOTS (0-15) NOW READ FROM SD CARD ---
    if (!sd_is_mounted) {
        Error_Raise(ERR_SD_MOUNT_FAIL, SEV_CRITICAL, "Cannot program FPGA, SD missing");
        return HAL_ERROR;
    }

    char path[20];
    snprintf(path, sizeof(path), "0:/slot%d.bin", slot);

    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) {
    	Error_Raise(ERR_SD_FILE_NOT_FOUND, SEV_CRITICAL, path);
    	return HAL_ERROR;
    }

    static uint8_t buffer[4096]; // Static keeps it in SRAM for DMA
    UINT br;
    uint8_t checksum = 0;

    while (f_read(&file, buffer, sizeof(buffer), &br) == FR_OK && br > 0) {
        for(UINT i = 0; i < br; i++) checksum ^= buffer[i];

        // Start DMA Transfer
        if (HAL_SPI_Transmit_DMA(&hspi3, buffer, br) != HAL_OK) {
            Error_Raise(ERR_SPI_DMA_TIMEOUT, SEV_CRITICAL, "DMA Start Failure");
            f_close(&file);
            return HAL_ERROR;
        }

        // Yield RTOS Task until DMA finishes
        if (osSemaphoreAcquire(FpgaDmaSemaphore, 1000) != osOK) {
        	Error_Raise(ERR_SPI_DMA_TIMEOUT, SEV_FATAL, "FPGA Bitstream Transfer");
        	HAL_SPI_Abort(&hspi3);
            f_close(&file);
            return HAL_ERROR;
        }
    }
    f_close(&file);
    USB_Printf(">>> SD CHECKSUM: 0x%02X <<<\n", checksum);

    // --- FINALIZATION: Send Dummy Clocks ---
    HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_SET);
    static uint8_t dummy[16];
    memset(dummy, 0xFF, sizeof(dummy));

    if (HAL_SPI_Transmit_DMA(&hspi3, dummy, 16) == HAL_OK) {
        osSemaphoreAcquire(FpgaDmaSemaphore, 100);
    }
    osDelay(5);

    if (HAL_GPIO_ReadPin(FPGA_nDONE_GPIO_Port, FPGA_nDONE_Pin) == GPIO_PIN_SET) {
        USB_Printf(">>> SUCCESS: FPGA RUNNING <<<\n");
        return HAL_OK;
    } else {
        Error_Raise(ERR_FPGA_DONE_LOW, SEV_CRITICAL, "nDONE pin stayed LOW");
        return HAL_ERROR;
    }
}

/**
 * @brief Puts the FPGA into a hard reset/power-down state.
 */
void FPGA_PowerDown(void) {
    // 1. Assert Reset LOW (Stops the FPGA internal state machines)
    HAL_GPIO_WritePin(FPGA_RST_GPIO_Port, FPGA_RST_Pin, GPIO_PIN_RESET);

    // 2. Drive CS HIGH to prevent SPI bus leakage
    HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_SET);

    // 3. Force MCO pin to NO CLOCK to save power and stop the FPGA fabric
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_NOCLOCK, RCC_MCODIV_1);
}
