/**
 * @file powerMonitor.c
 * @brief Optimized Implementation of the Power Monitor Driver (INA)
 */

#include "powerMonitor.h"
#include "cmsis_os.h"
#include <math.h>

/* --- Internal Hardware Registers --- */
#define REG_CONFIG       0x00
#define REG_SHUNTVOLTAGE 0x01
#define REG_BUSVOLTAGE   0x02
#define REG_POWER        0x03
#define REG_CURRENT      0x04
#define REG_CALIBRATION  0x05

#define CONFIG_RESET     0x8000
#define CONFIG_GAIN_MASK 0x1800

extern osMutexId_t I2c1Mutex;

/* ============================================================================
 * LOW-LEVEL I2C HELPERS (Thread-Safe Blocking)
 * ============================================================================ */

static HAL_StatusTypeDef Read16(PowerMonitor_t *dev, uint8_t reg, uint16_t *value) {
    uint8_t buf[2];

    if (osMutexAcquire(I2c1Mutex, 100) != osOK) return HAL_BUSY;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(dev->hi2c, (dev->address << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
    if (status == HAL_OK) {
        *value = (buf[0] << 8) | buf[1];
    }

    osMutexRelease(I2c1Mutex);
    return status;
}

static HAL_StatusTypeDef Write16(PowerMonitor_t *dev, uint8_t reg, uint16_t value) {
    uint8_t buf[2] = { (value >> 8) & 0xFF, value & 0xFF };

    if (osMutexAcquire(I2c1Mutex, 100) != osOK) return HAL_BUSY;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(dev->hi2c, (dev->address << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);

    osMutexRelease(I2c1Mutex);
    return status;
}

/* ============================================================================
 * CORE API
 * ============================================================================ */

HAL_StatusTypeDef PowerMonitor_Init(PowerMonitor_t *dev, I2C_HandleTypeDef *hi2c, uint8_t addr, float r_shunt_ohms, float max_expected_A) {
    if (!dev || !hi2c) return HAL_ERROR;

    dev->hi2c = hi2c;
    dev->address = addr;
    dev->r_shunt_ohms = r_shunt_ohms;

    /* Issue Soft Reset */
    Write16(dev, REG_CONFIG, CONFIG_RESET);
    osDelay(5); /* Yield to RTOS while device resets */

    /* Calculate Calibration Matrix */
    dev->current_lsb_A = max_expected_A / 32768.0f;
    float cal = 0.04096f / (dev->current_lsb_A * dev->r_shunt_ohms);
    dev->cal_value = (uint16_t)truncf(cal);

    if (Write16(dev, REG_CALIBRATION, dev->cal_value) != HAL_OK) return HAL_ERROR;

    /* Set default configuration: 32V Bus, 12-bit ADC, Continuous Mode */
    dev->current_pga = POWER_MONITOR_PGA_320MV;
    return Write16(dev, REG_CONFIG, 0x399F);
}

HAL_StatusTypeDef PowerMonitor_Read(PowerMonitor_t *dev, PowerMonitor_Data_t *data) {
    uint16_t bus_val, shunt_val, current_val;

    // The INA series does NOT auto-increment pointers across registers,
    // so we must read them individually. The Mutex inside Read16 keeps this thread-safe!
    if (Read16(dev, REG_BUSVOLTAGE, &bus_val) != HAL_OK) return HAL_ERROR;
    if (Read16(dev, REG_SHUNTVOLTAGE, &shunt_val) != HAL_OK) return HAL_ERROR;
    if (Read16(dev, REG_CURRENT, &current_val) != HAL_OK) return HAL_ERROR;

    data->overflow         = (bus_val & 0x01);
    data->bus_voltage_V    = (bus_val >> 3) * 0.004f;
    data->shunt_voltage_mV = ((int16_t)shunt_val) * 0.01f;
    // Calculate raw current, then subtract our hardware offset
	data->current_mA = ((((int16_t)current_val) * dev->current_lsb_A) * 1000.0f) - dev->current_offset_mA;

	// Clamp to 0.0f to prevent negative readings from ADC noise when idle
	if (data->current_mA < 0.0f) {
		data->current_mA = 0.0f;
	}

	// Power is calculated using the corrected current
	data->power_mW = data->bus_voltage_V * data->current_mA;

    return HAL_OK;
}

/* ============================================================================
 * ADVANCED HARDWARE CONTROL
 * ============================================================================ */

HAL_StatusTypeDef PowerMonitor_SetPGA(PowerMonitor_t *dev, PowerMonitor_PGA_t pga) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) != HAL_OK) return HAL_ERROR;

    config = (config & ~CONFIG_GAIN_MASK) | pga;
    if (Write16(dev, REG_CONFIG, config) == HAL_OK) {
        dev->current_pga = pga;
        return HAL_OK;
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef PowerMonitor_AutoScale(PowerMonitor_t *dev, const PowerMonitor_Data_t *current_data) {
    if (!dev || !current_data) return HAL_ERROR;

    float shunt_mV = fabsf(current_data->shunt_voltage_mV);
    PowerMonitor_PGA_t new_pga = dev->current_pga;

    /* Up-scale conditions */
    if (current_data->overflow || shunt_mV > 150.0f) {
        new_pga = POWER_MONITOR_PGA_320MV;
    } else if (shunt_mV > 75.0f && dev->current_pga < POWER_MONITOR_PGA_160MV) {
        new_pga = POWER_MONITOR_PGA_160MV;
    } else if (shunt_mV > 38.0f && dev->current_pga < POWER_MONITOR_PGA_80MV) {
        new_pga = POWER_MONITOR_PGA_80MV;
    }

    /* Down-scale conditions (with hard hysteresis) */
    if (!current_data->overflow) {
        if (dev->current_pga == POWER_MONITOR_PGA_320MV && shunt_mV < 120.0f) {
            new_pga = POWER_MONITOR_PGA_160MV;
        } else if (dev->current_pga == POWER_MONITOR_PGA_160MV && shunt_mV < 60.0f) {
            new_pga = POWER_MONITOR_PGA_80MV;
        } else if (dev->current_pga == POWER_MONITOR_PGA_80MV && shunt_mV < 30.0f) {
            new_pga = POWER_MONITOR_PGA_40MV;
        }
    }

    /* Apply new PGA setting if changed */
    if (new_pga != dev->current_pga) {
        return PowerMonitor_SetPGA(dev, new_pga);
    }

    return HAL_OK;
}

HAL_StatusTypeDef PowerMonitor_SetFastMode(PowerMonitor_t *dev, uint8_t enable) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) != HAL_OK) return HAL_ERROR;

    // Clear BADC (bits 10:7) and SADC (bits 6:3)
    config &= ~0x07F8;

    if (enable) {
        // 10-bit mode (148us) -> BADC=0001, SADC=0001
        config |= 0x0088;
    } else {
        // 12-bit mode (532us) -> BADC=0011, SADC=0011
        config |= 0x0198;
    }

    return Write16(dev, REG_CONFIG, config);
}

HAL_StatusTypeDef PowerMonitor_SetSleep(PowerMonitor_t *dev, uint8_t sleep) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) != HAL_OK) return HAL_ERROR;

    if (sleep) {
        config &= ~0x0007; /* 000 = Power-Down Mode */
    } else {
        config |= 0x0007;  /* 111 = Shunt and Bus Continuous Mode */
    }
    return Write16(dev, REG_CONFIG, config);
}

HAL_StatusTypeDef PowerMonitor_TriggerSingleShot(PowerMonitor_t *dev) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) != HAL_OK) return HAL_ERROR;

    config &= ~0x0007;
    config |= 0x0003; /* 011 = Shunt and Bus Voltage, Triggered */
    return Write16(dev, REG_CONFIG, config);
}

void PowerMonitor_SetOffset(PowerMonitor_t *dev, float offset_mA) {
    if (dev) dev->current_offset_mA = offset_mA;
}
