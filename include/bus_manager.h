/**
 * @file bus_manager.h
 * @brief Mediates shared hardware bus access to ensure thread safety.
 */
#ifndef BUS_MANAGER_H
#define BUS_MANAGER_H

/**
 * @brief Acquires the shared I2C bus mutex.
 * @return 0 on success, negative errno (-ENOLCK) on failure.
 */
int BusManager_LockI2C(void);

/**
 * @brief Releases the shared I2C bus mutex.
 */
void BusManager_UnlockI2C(void);

#endif /* BUS_MANAGER_H */