#include "main.h"
#include "ff.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "usbd_cdc_if.h"

extern void USB_Printf(const char *format, ...);

#define INACTIVE_BANK_ADDRESS 0x08040000
#define RTC_BKP_OTA_MAGIC     0x4F544150 // "OTAP" (OTA Pending)

void OTA_Printf(const char *format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    uint8_t retries = 50;
    while (CDC_Transmit_FS((uint8_t*)buffer, strlen(buffer)) == USBD_BUSY && retries > 0) {
        osDelay(2);
        retries--;
    }
    osDelay(5); // Give the USB hardware time to push the packet out
}


extern void USB_Printf(const char *format, ...);

HAL_StatusTypeDef OTA_Update_From_SD(const char* filename) {
    FIL file; UINT br;
    uint32_t flash_address = INACTIVE_BANK_ADDRESS;

    if (f_open(&file, filename, FA_READ) != FR_OK) {
    	OTA_Printf("OTA_LOG: ERR: OTA file not found.\n");
        return HAL_ERROR;
    }

    uint32_t total_size = f_size(&file);
    if (total_size < 1024) {
    	OTA_Printf("OTA_LOG: ERR: File too small.\n");
        f_close(&file); return HAL_ERROR;
    }

    OTA_Printf("OTA_LOG:Starting Dual-Bank OTA. Unlocking Flash...\n");
    HAL_FLASH_Unlock();

    OTA_Printf("OTA_LOG: Erasing Inactive Bank (Please wait)...\n");
    for (uint32_t i = 0; i < 128; i++) {
        FLASH_EraseInitTypeDef EraseInitStruct;
        uint32_t PageError;
        EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
        EraseInitStruct.NbPages = 1;
        EraseInitStruct.Page = i;

        if (READ_BIT(FLASH->OPTR, FLASH_OPTR_nSWAP_BANK)) EraseInitStruct.Banks = FLASH_BANK_2;
        else EraseInitStruct.Banks = FLASH_BANK_1;

        HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);
        osDelay(2); // Yield to keep USB alive

        // Print progress every 32 pages
        if (i % 32 == 0) OTA_Printf("OTA_LOG: Erase Progress: %d%%\n", (i * 100) / 128);
    }

    OTA_Printf("OTA_LOG: Erase Complete. Flashing %lu bytes...\n", total_size);

    uint8_t buffer[512];
    uint32_t bytes_written = 0;
    uint8_t last_pct = 0;

    while (f_read(&file, buffer, sizeof(buffer), &br) == FR_OK && br > 0) {
        if (br % 8 != 0) { memset(&buffer[br], 0xFF, 8 - (br % 8)); br += 8 - (br % 8); }

        for (uint32_t i = 0; i < br; i += 8) {
            uint64_t double_word_data = *(uint64_t*)(&buffer[i]);
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, flash_address, double_word_data);
            flash_address += 8;
        }
        bytes_written += br;

        // Stream live progress back to the Vue UI terminal!
        uint8_t pct = (bytes_written * 100) / total_size;
        if (pct >= last_pct + 10) { // Print every 10%
        	OTA_Printf("OTA_LOG: Flash Progress: %d%%\n", pct);
            last_pct = pct;
        }
        osDelay(1);
    }
    f_close(&file);

    // Validate Firmware
    uint32_t *new_fw_sp = (uint32_t*)INACTIVE_BANK_ADDRESS;
    uint32_t *new_fw_rv = (uint32_t*)(INACTIVE_BANK_ADDRESS + 4);

    // Check if Stack Pointer top byte matches RAM (0x20) and Reset Vector matches Flash (0x08)
    if ((*new_fw_sp & 0xFF000000) != 0x20000000 || (*new_fw_rv & 0xFF000000) != 0x08000000) {
    	OTA_Printf("OTA_LOG: ERR: Firmware validation failed. Aborting.\n");
        HAL_FLASH_Lock(); return HAL_ERROR;
    }

    OTA_Printf("OTA_LOG: Flashing Verified. Setting Rollback Watchdog...\n");

    // --- SET OTA PENDING FLAG IN BACKUP REGISTER ---
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    TAMP->BKP0R = RTC_BKP_OTA_MAGIC; // Set the flag

    OTA_Printf("OTA_LOG: Swapping Banks and Rebooting!\n");
    osDelay(100);

    HAL_FLASH_OB_Unlock();
    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY)) {}
    if (READ_BIT(FLASH->OPTR, FLASH_OPTR_nSWAP_BANK)) CLEAR_BIT(FLASH->OPTR, FLASH_OPTR_nSWAP_BANK);
    else SET_BIT(FLASH->OPTR, FLASH_OPTR_nSWAP_BANK);

    SET_BIT(FLASH->CR, FLASH_CR_OPTSTRT);
    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY)) {}
    HAL_FLASH_OB_Launch(); // Applies Option Bytes & reboots

    return HAL_OK;
}

