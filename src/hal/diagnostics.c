/**
 * @file diagnostics.c
 * @brief Benchmark and physical diagnostic test suites.
 */
#include "diagnostics.h"
#include "powerMonitor.h"
#include "rtos/usb_task.h"
#include "system_config.h"

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <string.h>

extern PowerMonitor_t ina_1v2_core;
extern PowerMonitor_t ina_3v3_ext;
extern PowerMonitor_t ina_3v3_fpga;
extern PowerMonitor_t ina_3v3_mcu;

void Diag_RunI2CBenchmark(void) {
    USB_Printf("LOG: Starting I2C Polling Stress Test (%lu Reads)...\n", DIAG_I2C_TEST_ITERATIONS * 4);

    PowerMonitor_Data_t dummy_data;
    uint32_t start_time = k_uptime_get_32();
    uint32_t fail_count = 0;

    for (uint32_t i = 0; i < DIAG_I2C_TEST_ITERATIONS; i++) { 
        if (PowerMonitor_Read(&ina_1v2_core, &dummy_data) != 0) fail_count++;
        if (PowerMonitor_Read(&ina_3v3_ext, &dummy_data) != 0) fail_count++;
        if (PowerMonitor_Read(&ina_3v3_fpga, &dummy_data) != 0) fail_count++;
        if (PowerMonitor_Read(&ina_3v3_mcu, &dummy_data) != 0) fail_count++;
        
        if (i % DIAG_I2C_YIELD_MODULUS == 0) k_yield();
    }

    uint32_t end_time = k_uptime_get_32();
    float time_s = (end_time - start_time) / 1000.0f;
    float reads_per_sec = DIAG_I2C_EXPECTED_READS / time_s;

    USB_Printf("LOG: --- I2C BENCHMARK RESULTS ---\n");
    USB_Printf("LOG: Total Time  : %.3f sec\n", (double)time_s);
    USB_Printf("LOG: Speed       : %.1f sensors/sec\n", (double)reads_per_sec);
    USB_Printf("LOG: Failed Reads: %lu\n", fail_count);
}


void Diag_RunSDCardTest(void) { USB_Printf("LOG: SD Diagnostics Starting...\n"); }
void Diag_RunRawSpeedTest(void) { USB_Printf("LOG: SD Raw Speed Test...\n"); }
void Diag_RunRawSectorTest(void) { USB_Printf("LOG: SD Sector Test...\n"); }
void Diag_RunUSBReadTest(void) { USB_Printf("LOG: USB Uplink Test...\n"); }