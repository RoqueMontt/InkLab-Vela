#include "diagnostics.h"
#include "main.h"
#include "ff.h"
#include "cmsis_os.h"
#include "user_diskio_spi.h"
#include <string.h>
#include "usbd_def.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "error_manager.h"
#include "powerMonitor.h"


extern void USB_Printf(const char *format, ...);
extern SPI_HandleTypeDef hspi3;
extern osSemaphoreId_t FpgaDmaSemaphore;
extern uint8_t sd_write_buffer[2][8192]; // Shared from frontend_api

extern PowerMonitor_t ina_1v2_core, ina_3v3_ext, ina_3v3_fpga, ina_3v3_stm32;

void Diag_RunI2CBenchmark(void) {
    USB_Printf("LOG: Starting I2C Polling Stress Test (4,000 Reads)...\n");

    PowerMonitor_Data_t dummy_data;
    uint32_t start_time = HAL_GetTick();
    uint32_t fail_count = 0;

    for (uint32_t i = 0; i < 1000; i++) {
        if (PowerMonitor_Read(&ina_1v2_core, &dummy_data) != HAL_OK) fail_count++;
        if (PowerMonitor_Read(&ina_3v3_ext, &dummy_data) != HAL_OK) fail_count++;
        if (PowerMonitor_Read(&ina_3v3_fpga, &dummy_data) != HAL_OK) fail_count++;
        if (PowerMonitor_Read(&ina_3v3_stm32, &dummy_data) != HAL_OK) fail_count++;

        // Yield every 50 loops so the USB task can keep the connection alive
        if (i % 50 == 0) osThreadYield();
    }

    uint32_t end_time = HAL_GetTick();
    float time_s = (end_time - start_time) / 1000.0f;
    float reads_per_sec = 4000.0f / time_s;

    USB_Printf("LOG: --- I2C BENCHMARK RESULTS ---\n");
    USB_Printf("LOG: Total Time  : %.3f sec\n", time_s);
    USB_Printf("LOG: Speed       : %.1f sensors/sec\n", reads_per_sec);
    USB_Printf("LOG: Failed Reads: %lu\n", fail_count);
    USB_Printf("LOG: -----------------------------\n");
}
// ==========================================
// SPI BENCHMARKS
// ==========================================
void Diag_RunSPIBenchmark(void) {
    USB_Printf("LOG: Starting SPI DMA Benchmark (10,000 packets)...\n");
    uint32_t iterations = 10000;
    uint32_t start_time = HAL_GetTick();

    uint8_t dummy_tx[64] = {0};
    uint8_t dummy_rx[64] = {0};

    for (uint32_t i = 0; i < iterations; i++) {
        HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_RESET);
        if (HAL_SPI_TransmitReceive_DMA(&hspi3, dummy_tx, dummy_rx, 64) == HAL_OK) {
            osSemaphoreAcquire(FpgaDmaSemaphore, 100);
        }
        HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_SET);
    }

    uint32_t end_time = HAL_GetTick();
    float time_s = (end_time - start_time) / 1000.0f;
    float speed_kbs = ((iterations * 64) / 1024.0f) / time_s;

    USB_Printf("LOG: --- BENCHMARK RESULTS ---\n");
    USB_Printf("LOG: Time: %.2f sec\n", time_s);
    USB_Printf("LOG: Speed: %.2f KB/s\n", speed_kbs);
    USB_Printf("LOG: -------------------------\n");
}

void Diag_RunSPIBulk(void) {
    USB_Printf("LOG: Starting BULK DMA Benchmark (400 KB)...\n");
    uint32_t iterations = 100;
    uint32_t start_time = HAL_GetTick();

    HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_RESET);
    for (uint32_t i = 0; i < iterations; i++) {
        if (HAL_SPI_Transmit_DMA(&hspi3, sd_write_buffer[0], 4096) == HAL_OK) {
            osSemaphoreAcquire(FpgaDmaSemaphore, 1000);
        }
    }
    HAL_GPIO_WritePin(FPGA_CS_GPIO_Port, FPGA_CS_Pin, GPIO_PIN_SET);

    uint32_t end_time = HAL_GetTick();
    float time_s = (end_time - start_time) / 1000.0f;
    if (time_s < 0.001f) time_s = 0.001f;

    float speed_kbs = ((iterations * 4096) / 1024.0f) / time_s;

    USB_Printf("LOG: --- BULK BENCHMARK RESULTS ---\n");
    USB_Printf("LOG: Size: 400 KB\n");
    USB_Printf("LOG: Time: %.3f sec\n", time_s);
    USB_Printf("LOG: Speed: %.2f KB/s\n", speed_kbs);
    USB_Printf("LOG: ----------------------------\n");
}

// ==========================================
// SD CARD DIAGNOSTICS
// ==========================================
void Diag_RunSDCardTest(void) {
    FIL testFil;
    UINT bw, br;
    FRESULT res;
    char testData[] = "SD_RTOS_TEST_SUCCESSFUL!";
    char readData[32] = {0};

    USB_Printf("--- SD CARD DIAGNOSTICS ---\n");

    DWORD fre_clust, fre_sect, tot_sect;
    FATFS *pfs;
    res = f_getfree("0:", &fre_clust, &pfs);
    if (res != FR_OK) {
        Error_Raise(ERR_SD_MOUNT_FAIL, SEV_CRITICAL, "f_getfree failed on diagnostic");
        return;
    }

    tot_sect = (pfs->n_fatent - 2) * pfs->csize;
    fre_sect = fre_clust * pfs->csize;
    USB_Printf("CAPACITY: %lu MB Total, %lu MB Free\n", tot_sect / 2048, fre_sect / 2048);

    res = f_open(&testFil, "0:/rtos_test.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (res == FR_OK) {
        f_write(&testFil, testData, strlen(testData), &bw);
        f_close(&testFil);
        USB_Printf("WRITE: OK (%d bytes written)\n", bw);
    } else {
        USB_Printf("WRITE: FAILED (Code %d)\n", res);
    }

    res = f_open(&testFil, "0:/rtos_test.txt", FA_READ);
    if (res == FR_OK) {
        f_read(&testFil, readData, sizeof(readData)-1, &br);
        f_close(&testFil);
        if (strncmp(testData, readData, strlen(testData)) == 0) USB_Printf("READ: OK (Data matched!)\n");
        else USB_Printf("READ: CORRUPT (Got: %s)\n", readData);
    } else {
        USB_Printf("READ: FAILED (Code %d)\n", res);
    }
    f_unlink("0:/rtos_test.txt");
    USB_Printf("--- DIAGNOSTICS END ---\n");
}

void Diag_RunRawSpeedTest(void) {
    FIL testFil;
    UINT bw, br;
    uint32_t start_time, end_time;
    const uint32_t TEST_SIZE = 1024 * 1024; // 1MB
    const uint32_t CHUNK_SIZE = 4096;

    memset(sd_write_buffer[0], 0xAA, CHUNK_SIZE);
    USB_Printf("\n--- RAW SD CARD SPEED TEST (1MB) ---\n");

    if (f_open(&testFil, "0:/speed.bin", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        USB_Printf("ERR: Open for write failed.\n");
        return;
    }

    USB_Printf("Writing 1MB to SD Card...\n");
    start_time = HAL_GetTick();
    for (uint32_t i = 0; i < TEST_SIZE; i += CHUNK_SIZE) {
        f_write(&testFil, sd_write_buffer[0], CHUNK_SIZE, &bw);
        if(i % (128 * 1024) == 0) osThreadYield();
    }
    f_sync(&testFil);
    end_time = HAL_GetTick();
    f_close(&testFil);

    float write_speed = 1024.0f / ((end_time - start_time) / 1000.0f);

    if (f_open(&testFil, "0:/speed.bin", FA_READ) != FR_OK) return;

    USB_Printf("Reading 1MB from SD Card...\n");
    start_time = HAL_GetTick();
    for (uint32_t i = 0; i < TEST_SIZE; i += CHUNK_SIZE) {
        f_read(&testFil, sd_write_buffer[0], CHUNK_SIZE, &br);
        if(i % (128 * 1024) == 0) osThreadYield();
    }
    end_time = HAL_GetTick();
    f_close(&testFil);

    float read_speed = 1024.0f / ((end_time - start_time) / 1000.0f);

    USB_Printf("=== RESULTS ===\nWrite Speed: %.2f KB/s\nRead Speed : %.2f KB/s\n=================\n", write_speed, read_speed);
}

void Diag_RunRawSectorTest(void) {
    uint32_t start_time, end_time;
    USB_Printf("\n--- PURE PHYSICAL SPI DMA TEST (1MB) ---\nBypassing FatFs... Reading raw physical sectors.\n");

    start_time = HAL_GetTick();
    for (uint32_t i = 0; i < 2048; i += 16) {
        USER_SPI_read(0, sd_write_buffer[0], 10000 + i, 16);
        if (i % 256 == 0) osThreadYield();
    }
    end_time = HAL_GetTick();

    float read_speed = 1024.0f / ((end_time - start_time) / 1000.0f);
    USB_Printf("=== HARDWARE LIMITS ===\nRaw SPI Read Speed : %.2f KB/s\n=======================\n", read_speed);
}

void Diag_RunUSBReadTest(void) {
    extern USBD_HandleTypeDef hUsbDeviceFS;
    FIL file;
    UINT br;
    uint32_t total_read = 0;

    if (f_open(&file, "0:/speed.bin", FA_READ) != FR_OK) {
        USB_Printf("ERR: Run RAW_TEST first to generate 1MB speed.bin\n");
        return;
    }

    USB_Printf("START_READ_TEST\n");
    uint32_t start_time = HAL_GetTick();

    while (f_read(&file, sd_write_buffer[0], 4096, &br) == FR_OK && br > 0) {
        USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
        while(hcdc->TxState != 0) osThreadYield();
        CDC_Transmit_FS(sd_write_buffer[0], br);
        total_read += br;
    }

    uint32_t end_time = HAL_GetTick();
    f_close(&file);

    float speed = (total_read / 1024.0f) / ((end_time - start_time) / 1000.0f);
    osDelay(10);
    USB_Printf("\n=== MCU UPLINK CALCULATION ===\nSize  : %lu bytes\nSpeed : %.2f KB/s\n==============================\n", total_read, speed);
}
