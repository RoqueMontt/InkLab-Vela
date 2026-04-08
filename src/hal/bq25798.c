/**
 * @file bq25798.c
 * @brief Thread-safe driver for TI BQ25798 Integrated Charger.
 */
#include "bq25798.h"
#include "bus_manager.h"
#include "system_config.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>  
#include <errno.h>

#define REG00_MIN_SYS_VOLTAGE       0x00
#define REG01_CHARGE_VOLTAGE        0x01
#define REG03_CHARGE_CURRENT        0x03
#define REG05_VINDPM                0x05
#define REG06_IINDPM                0x06
#define REG09_TERMINATION_CONTROL   0x09
#define REG0B_VOTG_REGULATION       0x0B
#define REG0D_IOTG_REGULATION       0x0D
#define REG0F_CHARGER_CTRL_0        0x0F
#define REG10_CHARGER_CTRL_1        0x10
#define REG11_CHARGER_CTRL_2        0x11
#define REG12_CHARGER_CTRL_3        0x12
#define REG13_CHARGER_CTRL_4        0x13
#define REG14_CHARGER_CTRL_5        0x14
#define REG16_TEMP_CONTROL          0x16
#define REG1B_CHG_STAT_0            0x1B
#define REG2E_ADC_CONTROL           0x2E
#define REG31_IBUS_ADC              0x31

static struct i2c_dt_spec bq_i2c;
static uint8_t bq_auto_rearm_enabled = 1;
static float bq_ema_coeff = 0.8f;

/** @brief Internal wrapper for thread-safe single byte write */
static int bq_write_byte(uint8_t reg, uint8_t value) {
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    int ret = i2c_reg_write_byte_dt(&bq_i2c, reg, value);
    BusManager_UnlockI2C();
    return ret;
}

/** @brief Internal wrapper for thread-safe word write */
static int bq_write_word(uint8_t reg, uint16_t value) {
    uint8_t buf[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    int ret = i2c_burst_write_dt(&bq_i2c, reg, buf, 2);
    BusManager_UnlockI2C();
    return ret;
}

/** @brief Internal wrapper for thread-safe word read */
static int bq_read_word(uint8_t reg, int16_t* value) {
    uint8_t buf[2];
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    int status = i2c_burst_read_dt(&bq_i2c, reg, buf, 2);
    BusManager_UnlockI2C();
    
    if (status == 0) *value = (int16_t)((buf[0] << 8) | buf[1]);
    return status;
}

/** @brief Internal wrapper for thread-safe bitmask update */
static int bq_update_bits(uint8_t reg, uint8_t mask, uint8_t value) {
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    int ret = i2c_reg_update_byte_dt(&bq_i2c, reg, mask, value);
    BusManager_UnlockI2C();
    return ret;
}

int BQ25798_Init(const struct device *i2c_bus) {
    if (!device_is_ready(i2c_bus)) return -ENODEV;

    bq_i2c.bus = i2c_bus;
    bq_i2c.addr = BQ_I2C_ADDR;

    bq_update_bits(REG10_CHARGER_CTRL_1, 0x07, 0x00); /* Disable Watchdog */
    bq_update_bits(REG13_CHARGER_CTRL_4, (1<<5), (1<<5)); /* 750kHz */
    bq_update_bits(REG12_CHARGER_CTRL_3, (1<<4), (1<<4)); /* Disable PFM */
    bq_update_bits(REG14_CHARGER_CTRL_5, (1<<5)|(1<<1), (1<<5)); /* EN_IBAT */
    bq_update_bits(REG11_CHARGER_CTRL_2, (1<<6), 0); /* Disable INDET */

    BQ25798_SetInputCurrent(BQ_DEFAULT_ICHG_MA);
    return bq_write_byte(REG2E_ADC_CONTROL, BQ_ADC_CTRL_ENABLE); /* Enable Continuous ADC */
}

int BQ25798_ReadAll(BQ25798_Data_t* data) {
    uint8_t stat_fault_burst[7];
    uint8_t adc_burst[12];
    static float ema_ibat = 0.0f;
    static float ema_ibus = 0.0f;
    static bool first_read = true;
    int ret;

    bq_update_bits(REG2E_ADC_CONTROL, 0x80, 0x80);

    if (BusManager_LockI2C() != 0) return -ENOLCK;
    ret = i2c_burst_read_dt(&bq_i2c, REG1B_CHG_STAT_0, stat_fault_burst, 7);
    if (ret == 0) ret = i2c_burst_read_dt(&bq_i2c, REG31_IBUS_ADC, adc_burst, 12);
    if (ret == 0) ret = i2c_reg_read_byte_dt(&bq_i2c, REG0F_CHARGER_CTRL_0, &data->ctrl0);
    BusManager_UnlockI2C();

    if (ret < 0) return -EIO;

    data->stat0  = stat_fault_burst[0];
    data->stat1  = stat_fault_burst[1];
    data->fault0 = stat_fault_burst[5];
    data->fault1 = stat_fault_burst[6];

    int16_t raw_ibus = (int16_t)((adc_burst[0] << 8) | adc_burst[1]);
    int16_t raw_ibat = (int16_t)((adc_burst[2] << 8) | adc_burst[3]);
    int16_t raw_vbus = (int16_t)((adc_burst[4] << 8) | adc_burst[5]);
    int16_t raw_vbat = (int16_t)((adc_burst[10] << 8) | adc_burst[11]);

    float current_ibat = (float)raw_ibat;
    float current_ibus = (float)raw_ibus;

    if (first_read) {
        ema_ibat = current_ibat;
        ema_ibus = current_ibus;
        first_read = false;
    } else {
        ema_ibat = (bq_ema_coeff * current_ibat) + ((1.0f - bq_ema_coeff) * ema_ibat);
        ema_ibus = (bq_ema_coeff * current_ibus) + ((1.0f - bq_ema_coeff) * ema_ibus);
    }

    data->vbus_V  = (float)raw_vbus / 1000.0f;
    data->vbat_V  = (float)raw_vbat / 1000.0f;
    data->ibus_mA = ema_ibus;
    data->ibat_mA = ema_ibat;
    data->power_mW = data->vbat_V * data->ibat_mA;
    data->vbus_power_mW = data->vbus_V * data->ibus_mA;

    const char* modes[] = {"Idle", "Trickle", "Pre-chg", "Fast", "Taper", "Res", "Top-off", "Term"};
    uint8_t m_idx = (data->stat1 >> 5) & 0x07;
    snprintf(data->status_str, sizeof(data->status_str), "%s", modes[m_idx]);

    uint8_t vbus_stat = (data->stat1 >> 1) & 0x0F;
    if (vbus_stat == 0x07 || vbus_stat == 0x0C) {
        snprintf(data->status_str, sizeof(data->status_str), "OTG");
    }

    return 0;
}

int BQ25798_DumpRegisters(char* buffer, size_t max_len) {
    uint8_t regs[BQ_REG_DUMP_COUNT];
    
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    int ret = i2c_burst_read_dt(&bq_i2c, 0x00, regs, BQ_REG_DUMP_COUNT);
    BusManager_UnlockI2C();
    
    if (ret < 0) return -EIO;

    int offset = snprintf(buffer, max_len, "{\"type\":\"bq_dump\",\"regs\":[");
    for (int i = 0; i < BQ_REG_DUMP_COUNT; i++) {
        int written = snprintf(buffer + offset, max_len - offset, "%d%s", regs[i], (i == (BQ_REG_DUMP_COUNT - 1)) ? "" : ",");
        if (written > 0 && written < (max_len - offset)) offset += written;
        else break;
    }
    snprintf(buffer + offset, max_len - offset, "],\"rearm\":%d,\"ema\":%.2f}", bq_auto_rearm_enabled, (double)bq_ema_coeff);
    return 0;
}

int BQ25798_SetChargeCurrent(uint16_t current_mA) { return bq_write_word(REG03_CHARGE_CURRENT, current_mA / 10); }
int BQ25798_SetInputCurrent(uint16_t current_mA)  { return bq_write_word(REG06_IINDPM, current_mA / 10); }
int BQ25798_SetChargeVoltage(uint16_t voltage_mV) { return bq_write_word(REG01_CHARGE_VOLTAGE, voltage_mV / 10); }
int BQ25798_SetInputVoltageLimit(uint16_t voltage_mV) { return bq_write_byte(REG05_VINDPM, voltage_mV / 100); }
int BQ25798_SetTermCurrent(uint16_t current_mA) { return bq_update_bits(REG09_TERMINATION_CONTROL, 0x1F, current_mA / 40); }

int BQ25798_SetMinSystemVoltage(uint16_t voltage_mV) {
    if(voltage_mV < 2500) voltage_mV = 2500;
    return bq_update_bits(REG00_MIN_SYS_VOLTAGE, 0x3F, (voltage_mV - 2500) / 250);
}

int BQ25798_SetOTGVoltage(uint16_t voltage_mV) {
    if (voltage_mV < BQ_VOTG_BASE_MV) voltage_mV = BQ_VOTG_BASE_MV;
    if (voltage_mV > 5600) voltage_mV = 5600;
    return bq_write_word(REG0B_VOTG_REGULATION, (voltage_mV - BQ_VOTG_BASE_MV) / BQ_OTG_REG_DIVISOR);
}

int BQ25798_SetOTGCurrent(uint16_t current_mA) {
    if(current_mA < 160) current_mA = 160;
    return bq_update_bits(REG0D_IOTG_REGULATION, 0x7F, (current_mA - 160) / 40);
}

int BQ25798_AdjustVOTG(int step_mv) {
    int16_t raw;
    if (bq_read_word(REG0B_VOTG_REGULATION, &raw) == 0) {
        int new_mv = ((raw * BQ_OTG_REG_DIVISOR) + BQ_VOTG_BASE_MV) + step_mv;
        if(new_mv < 3600) new_mv = 3600;
        if(new_mv > 12000) new_mv = 12000;
        return BQ25798_SetOTGVoltage(new_mv);
    }
    return -EIO;
}

int BQ25798_RearmBackup(uint16_t delay_ms) {
    uint8_t val = 0;
    if (!bq_auto_rearm_enabled) return -EPERM;

    k_msleep(delay_ms);
    bq_update_bits(REG16_TEMP_CONTROL, 0x01, 0x01);
    k_msleep(100); 

    bool ac_valid = false;
    for (int i = 0; i < 30; i++) {
        k_msleep(100);
        if (BusManager_LockI2C() == 0) {
            int ret = i2c_reg_read_byte_dt(&bq_i2c, REG13_CHARGER_CTRL_4, &val);
            BusManager_UnlockI2C();
            if (ret == 0 && (val & (1 << 6))) { 
                 ac_valid = true; 
                 break; 
            }
        }
    }

    if (ac_valid) {
        bq_update_bits(REG12_CHARGER_CTRL_3, (1 << 6), 0);
        k_msleep(500); 
        bq_update_bits(REG0F_CHARGER_CTRL_0, (1 << 0), (1 << 0));
        printk("LOG: Backup Rearmed. Hand-off complete.\n");
        return 0;
    } else {
        bq_update_bits(REG16_TEMP_CONTROL, 0x01, 0);
        return -ETIMEDOUT;
    }
}

int BQ25798_SetChargeEnable(uint8_t enable) { return bq_update_bits(REG0F_CHARGER_CTRL_0, (1<<5), enable ? (1<<5) : 0); }
int BQ25798_SetHiZ(uint8_t enable) { return bq_update_bits(REG0F_CHARGER_CTRL_0, (1<<2), enable ? (1<<2) : 0); }
int BQ25798_SetOTG(uint8_t enable) { return bq_update_bits(REG12_CHARGER_CTRL_3, (1<<6), enable ? (1<<6) : 0); }
int BQ25798_SetInputDisconnect(uint8_t disconnect) { return bq_update_bits(REG12_CHARGER_CTRL_3, (1<<7), disconnect ? (1<<7) : 0); }
int BQ25798_SetShipMode(uint8_t mode) { return bq_update_bits(REG11_CHARGER_CTRL_2, (3<<1), (mode & 0x03) << 1); }

int BQ25798_EnableBackup5V5(void) {
    bq_write_byte(REG05_VINDPM, BQ_VINDPM_DEFAULT_MV / 100);
    bq_update_bits(REG10_CHARGER_CTRL_1, (3 << 6), (3 << 6));
    bq_write_word(REG0B_VOTG_REGULATION, (BQ_VOTG_BACKUP_MV - BQ_VOTG_BASE_MV) / BQ_OTG_REG_DIVISOR);
    bq_update_bits(REG14_CHARGER_CTRL_5, (3 << 3), (3 << 3));
    return bq_update_bits(REG0F_CHARGER_CTRL_0, (1<<0), (1<<0));
}

int BQ25798_SetBackup(uint8_t enable) { return bq_update_bits(REG0F_CHARGER_CTRL_0, (1<<0), enable ? (1<<0) : 0); }
int BQ25798_SetBackupACFET(uint8_t enable) { return bq_update_bits(REG16_TEMP_CONTROL, (1<<0), enable ? (1<<0) : 0); }
void BQ25798_SetAutoRearm(uint8_t enable) { bq_auto_rearm_enabled = enable; }
void BQ25798_ToggleAutoRearm(void) { bq_auto_rearm_enabled = !bq_auto_rearm_enabled; }
int BQ25798_SetPFM_FWD(uint8_t enable) { return bq_update_bits(REG12_CHARGER_CTRL_3, (1<<4), enable ? 0 : (1<<4)); }
int BQ25798_SetPFM_OTG(uint8_t enable) { return bq_update_bits(REG12_CHARGER_CTRL_3, (1<<5), enable ? 0 : (1<<5)); }

int BQ25798_TogglePFM(void) {
    uint8_t tmp;
    if (BusManager_LockI2C() != 0) return -ENOLCK;
    if (i2c_reg_read_byte_dt(&bq_i2c, REG12_CHARGER_CTRL_3, &tmp) == 0) {
        tmp ^= ((1<<4) | (1<<5));
        int ret = i2c_reg_write_byte_dt(&bq_i2c, REG12_CHARGER_CTRL_3, tmp);
        BusManager_UnlockI2C();
        return ret;
    }
    BusManager_UnlockI2C();
    return -EIO;
}

int BQ25798_SetWatchdog(uint8_t enable) { return bq_update_bits(REG10_CHARGER_CTRL_1, 0x07, enable ? 0x05 : 0x00); }
int BQ25798_ForceDetection(void) {
    bq_update_bits(REG11_CHARGER_CTRL_2, (1<<7), (1<<7));
    return bq_update_bits(REG0F_CHARGER_CTRL_0, (1<<3), (1<<3));
}
int BQ25798_SetADCResolution(uint8_t bits) {
    uint8_t val = 0x00; 
    if (bits == 14)      val = 0x10;
    else if (bits == 13) val = 0x20;
    else if (bits == 12) val = 0x30;
    return bq_update_bits(REG2E_ADC_CONTROL, 0x30, val);
}
void BQ25798_SetEMA(float coeff) {
    if (coeff < 0.1f) coeff = 0.1f;
    if (coeff > 1.0f) coeff = 1.0f;
    bq_ema_coeff = coeff;
}
void BQ25798_GetJson(BQ25798_Data_t* data, char* buffer, size_t max_len) {
    snprintf(buffer, max_len,
             "{\"type\":\"bq\",\"vbus\":%.2f,\"vbat\":%.2f,\"ibus\":%.1f,\"ibat\":%.1f,\"p\":%.1f,\"s\":\"%s\"}",
             (double)data->vbus_V, (double)data->vbat_V, (double)data->ibus_mA,
             (double)data->ibat_mA, (double)data->power_mW, data->status_str);
}