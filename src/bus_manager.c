/**
 * @file bus_manager.c
 * @brief Implementation of shared bus synchronization.
 */
#include "bus_manager.h"
#include <zephyr/kernel.h>
#include <errno.h>

/* Centralized mutex for the I2C1 Bus */
static K_MUTEX_DEFINE(i2c1_bus_mutex);

/** Internal helper to lock the I2C bus */
int BusManager_LockI2C(void) {
    if (k_mutex_lock(&i2c1_bus_mutex, K_FOREVER) == 0) {
        return 0;
    }
    return -ENOLCK;
}

/** Internal helper to unlock the I2C bus */
void BusManager_UnlockI2C(void) {
    k_mutex_unlock(&i2c1_bus_mutex);
}