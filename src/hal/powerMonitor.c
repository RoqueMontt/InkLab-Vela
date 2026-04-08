/**
 * @file powerMonitor.c
 * @brief Zephyr Implementation of the Power Monitor Driver (INA series).
 * @note Strictly enforces shared bus access via bus_manager.h (Law 2).
 */

#include "hal/powerMonitor.h"
#include "system_config.h"
#include "bus_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <math.h>

#define REG_CONFIG       0x00
#define REG_SHUNTVOLTAGE 0x01
#define REG_BUSVOLTAGE   0x02
#define REG_POWER        0x03
#define REG_CURRENT      0x04
#define REG_CALIBRATION  0x05

#define INA219_REG_RESET 0x8000
#define CONFIG_GAIN_MASK 0x1800

/** @brief Internal helper to safely read a 16-bit register over a shared I2C bus. */
static int Read16(PowerMonitor_t *dev, uint8_t reg, uint16_t *value) {
    uint8_t buf[2];
    
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    int status = i2c_burst_read(dev->i2c_bus, dev->address, reg, buf, 2);
    BusManager_UnlockI2C();
    
    if (status == 0) {
        *value = (buf[0] << 8) | buf[1];
    }
    return status;
}

/** @brief Internal helper to safely write a 16-bit register over a shared I2C bus. */
static int Write16(PowerMonitor_t *dev, uint8_t reg, uint16_t value) {
    uint8_t buf[2] = { (value >> 8) & 0xFF, value & 0xFF };
    
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    int status = i2c_burst_write(dev->i2c_bus, dev->address, reg, buf, 2);
    BusManager_UnlockI2C();
    
    return status;
}

int PowerMonitor_Init(PowerMonitor_t *dev, const struct device *i2c_bus, uint8_t addr, float r_shunt_ohms, float max_expected_A) {
    if (!dev || !i2c_bus) return -EINVAL;

    dev->i2c_bus = i2c_bus;
    dev->address = addr;
    dev->r_shunt_ohms = r_shunt_ohms;
    dev->current_offset_mA = 0.0f;

    if (!device_is_ready(dev->i2c_bus)) return -ENODEV;

    int ret = Write16(dev, REG_CONFIG, INA219_REG_RESET);
    if (ret < 0) return ret;

    k_msleep(5); /* Yield while device resets */

    dev->current_lsb_A = max_expected_A / INA_CURRENT_LSB_BASE;
    float cal = INA_CALIB_MAGIC / (dev->current_lsb_A * dev->r_shunt_ohms);
    dev->cal_value = (uint16_t)cal;

    ret = Write16(dev, REG_CALIBRATION, dev->cal_value);
    if (ret < 0) return ret;

    dev->current_pga = POWER_MONITOR_PGA_320MV;
    return Write16(dev, REG_CONFIG, INA_DEFAULT_CONFIG);
}

int PowerMonitor_Read(PowerMonitor_t *dev, PowerMonitor_Data_t *data) {
    uint16_t bus_val, shunt_val, current_val;

    if (Read16(dev, REG_BUSVOLTAGE, &bus_val) < 0) return -EIO;
    if (Read16(dev, REG_SHUNTVOLTAGE, &shunt_val) < 0) return -EIO;
    if (Read16(dev, REG_CURRENT, &current_val) < 0) return -EIO;

    data->overflow         = (bus_val & 0x01);
    data->bus_voltage_V    = (bus_val >> 3) * INA_BUS_V_MULTIPLIER;
    data->shunt_voltage_mV = ((int16_t)shunt_val) * INA_SHUNT_MV_MULTIPLIER;

    data->current_mA = ((((int16_t)current_val) * dev->current_lsb_A) * 1000.0f) - dev->current_offset_mA;

    if (data->current_mA < 0.0f) {
        data->current_mA = 0.0f;
    }

    data->power_mW = data->bus_voltage_V * data->current_mA;
    return 0;
}

int PowerMonitor_SetPGA(PowerMonitor_t *dev, PowerMonitor_PGA_t pga) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) < 0) return -EIO;

    config = (config & ~CONFIG_GAIN_MASK) | pga;
    if (Write16(dev, REG_CONFIG, config) == 0) {
        dev->current_pga = pga;
        return 0;
    }
    return -EIO;
}

int PowerMonitor_AutoScale(PowerMonitor_t *dev, const PowerMonitor_Data_t *current_data) {
    if (!dev || !current_data) return -EINVAL;

    float shunt_mV = fabsf(current_data->shunt_voltage_mV);
    PowerMonitor_PGA_t new_pga = dev->current_pga;

    if (current_data->overflow || shunt_mV > INA_AUTO_UP_320MV) {
        new_pga = POWER_MONITOR_PGA_320MV;
    } else if (shunt_mV > INA_AUTO_UP_160MV && dev->current_pga < POWER_MONITOR_PGA_160MV) {
        new_pga = POWER_MONITOR_PGA_160MV;
    } else if (shunt_mV > INA_AUTO_UP_80MV && dev->current_pga < POWER_MONITOR_PGA_80MV) {
        new_pga = POWER_MONITOR_PGA_80MV;
    }

    if (!current_data->overflow) {
        if (dev->current_pga == POWER_MONITOR_PGA_320MV && shunt_mV < INA_AUTO_DN_160MV) {
            new_pga = POWER_MONITOR_PGA_160MV;
        } else if (dev->current_pga == POWER_MONITOR_PGA_160MV && shunt_mV < INA_AUTO_DN_80MV) {
            new_pga = POWER_MONITOR_PGA_80MV;
        } else if (dev->current_pga == POWER_MONITOR_PGA_80MV && shunt_mV < INA_AUTO_DN_40MV) {
            new_pga = POWER_MONITOR_PGA_40MV;
        }
    }

    if (new_pga != dev->current_pga) {
        return PowerMonitor_SetPGA(dev, new_pga);
    }

    return 0;
}

int PowerMonitor_SetFastMode(PowerMonitor_t *dev, uint8_t enable) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) < 0) return -EIO;

    config &= ~0x07F8;

    if (enable) {
        config |= 0x0088; // 10-bit mode
    } else {
        config |= 0x0198; // 12-bit mode
    }

    return Write16(dev, REG_CONFIG, config);
}

int PowerMonitor_SetSleep(PowerMonitor_t *dev, uint8_t sleep) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) < 0) return -EIO;

    if (sleep) {
        config &= ~0x0007; 
    } else {
        config |= 0x0007;  
    }
    return Write16(dev, REG_CONFIG, config);
}

int PowerMonitor_TriggerSingleShot(PowerMonitor_t *dev) {
    uint16_t config;
    if (Read16(dev, REG_CONFIG, &config) < 0) return -EIO;

    config &= ~0x0007;
    config |= 0x0003; 
    return Write16(dev, REG_CONFIG, config);
}

void PowerMonitor_SetOffset(PowerMonitor_t *dev, float offset_mA) {
    if (dev) dev->current_offset_mA = offset_mA;
}