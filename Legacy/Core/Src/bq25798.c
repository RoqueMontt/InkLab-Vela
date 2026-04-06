/**
 * @file bq25798.c
 * @brief Optimized & Thread-Safe Driver for TI BQ25798 Integrated Buck-Boost Charger
 */

#include "bq25798.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// REGISTER MAP & BITMASKS
// ============================================================================
#define REG00_MIN_SYS_VOLTAGE       0x00
#define REG01_CHARGE_VOLTAGE        0x01
#define REG03_CHARGE_CURRENT        0x03
#define REG05_VINDPM                0x05
#define REG06_IINDPM                0x06
#define REG09_TERMINATION_CONTROL   0x09
#define REG0B_VOTG_REGULATION       0x0B
#define REG0D_IOTG_REGULATION       0x0D

#define REG0F_CHARGER_CTRL_0        0x0F
#define BQ_CTRL0_EN_CHG             (1 << 5)
#define BQ_CTRL0_EN_HIZ             (1 << 2)
#define BQ_CTRL0_EN_BACKUP          (1 << 0)

#define REG10_CHARGER_CTRL_1        0x10
#define BQ_CTRL1_WD_MASK            0x07

#define REG11_CHARGER_CTRL_2        0x11
#define BQ_CTRL2_FORCE_INDET        (1 << 7)
#define BQ_CTRL2_AUTO_INDET_EN      (1 << 6)

#define REG12_CHARGER_CTRL_3        0x12
#define BQ_CTRL3_DIS_ACDRV          (1 << 7)
#define BQ_CTRL3_EN_OTG             (1 << 6)
#define BQ_CTRL3_DIS_OTG_PFM        (1 << 5)
#define BQ_CTRL3_DIS_FWD_PFM        (1 << 4)

#define REG13_CHARGER_CTRL_4        0x13
#define BQ_CTRL4_FSW_750KHZ         (1 << 5)

#define REG14_CHARGER_CTRL_5        0x14
#define BQ_CTRL5_EN_IBAT            (1 << 5)
#define BQ_CTRL5_EN_EXTILIM         (1 << 1)

#define REG16_TEMP_CONTROL          0x16
#define REG1B_CHG_STAT_0            0x1B
#define REG1C_CHG_STAT_1            0x1C
#define REG20_FAULT_STATUS_0        0x20
#define REG21_FAULT_STATUS_1        0x21
#define REG2E_ADC_CONTROL           0x2E
#define REG31_IBUS_ADC              0x31

#define BQ_ADC_CTRL_ENABLE       (1 << 7) // 0x80
#define BQ_ADC_RESOLUTION_MASK   (3 << 4) // 0x30
#define BQ_ADC_RES_15BIT         (0 << 4) // 0x00
#define BQ_ADC_RES_14BIT         (1 << 4) // 0x10
#define BQ_ADC_RES_13BIT         (2 << 4) // 0x20
#define BQ_ADC_RES_12BIT         (3 << 4) // 0x30

static I2C_HandleTypeDef* bq_i2c;
static uint8_t bq_auto_rearm_enabled = 1;
static float bq_ema_coeff = 0.8f;

extern osSemaphoreId_t I2c1DmaSemaphore;
extern osMutexId_t I2c1Mutex;

// ============================================================================
// LOW-LEVEL I2C HELPERS (Thread-Safe Blocking)
// ============================================================================

static HAL_StatusTypeDef bq_write_byte(uint8_t reg, uint8_t value) {
    HAL_StatusTypeDef status = HAL_BUSY;
    if (osMutexAcquire(I2c1Mutex, 100) == osOK) {
        status = HAL_I2C_Mem_Write(bq_i2c, BQ25798_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
        osMutexRelease(I2c1Mutex);
    }
    return status;
}

static HAL_StatusTypeDef bq_write_word(uint8_t reg, uint16_t value) {
    uint8_t buf[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    HAL_StatusTypeDef status = HAL_BUSY;
    if (osMutexAcquire(I2c1Mutex, 100) == osOK) {
        status = HAL_I2C_Mem_Write(bq_i2c, BQ25798_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
        osMutexRelease(I2c1Mutex);
    }
    return status;
}

static HAL_StatusTypeDef bq_read_word(uint8_t reg, int16_t* value) {
    uint8_t buf[2];
    HAL_StatusTypeDef status = HAL_BUSY;
    if (osMutexAcquire(I2c1Mutex, 100) == osOK) {
        status = HAL_I2C_Mem_Read(bq_i2c, BQ25798_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
        if (status == HAL_OK) *value = (int16_t)((buf[0] << 8) | buf[1]);
        osMutexRelease(I2c1Mutex);
    }
    return status;
}

static HAL_StatusTypeDef bq_update_bits(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t tmp; // No longer needs to be static since we aren't using DMA here

    if (osMutexAcquire(I2c1Mutex, 100) != osOK) return HAL_BUSY;

    if (HAL_I2C_Mem_Read(bq_i2c, BQ25798_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &tmp, 1, 100) == HAL_OK) {
        tmp = (tmp & ~mask) | (value & mask);
        HAL_I2C_Mem_Write(bq_i2c, BQ25798_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &tmp, 1, 100);
    } else {
        osMutexRelease(I2c1Mutex);
        return HAL_ERROR;
    }

    osMutexRelease(I2c1Mutex);
    return HAL_OK;
}

// ============================================================================
// INITIALIZATION & CORE CONFIGURATION
// ============================================================================

void BQ25798_Init(I2C_HandleTypeDef* hi2c) {
    bq_i2c = hi2c;

    // 1. Disable Watchdog to prevent unexpected resets during debugging
    bq_update_bits(REG10_CHARGER_CTRL_1, BQ_CTRL1_WD_MASK, 0x00);

    // 2. Switching Frequency: Set to 750kHz for better thermal performance
    bq_update_bits(REG13_CHARGER_CTRL_4, BQ_CTRL4_FSW_750KHZ, BQ_CTRL4_FSW_750KHZ);

    // 3. Disable PFM in Forward mode to maintain clean 3.3V power rails at light loads
    bq_update_bits(REG12_CHARGER_CTRL_3, BQ_CTRL3_DIS_FWD_PFM, BQ_CTRL3_DIS_FWD_PFM);

    // 4. Enable IBAT ADC reporting, disable hardware ILIM pin (Allow software control)
    bq_update_bits(REG14_CHARGER_CTRL_5, BQ_CTRL5_EN_IBAT | BQ_CTRL5_EN_EXTILIM, BQ_CTRL5_EN_IBAT);

    // 5. Disable D+/D- Auto-Detection (Skip slow USB handshakes)
    bq_update_bits(REG11_CHARGER_CTRL_2, BQ_CTRL2_AUTO_INDET_EN, 0);

    // 6. Force Default Input Current Limit (IINDPM) to 500mA
    BQ25798_SetInputCurrent(500);

    // 7. Enable Continuous ADC (Disable hardware averaging) -> 0x80
    bq_write_byte(REG2E_ADC_CONTROL, BQ_ADC_CTRL_ENABLE);


}

HAL_StatusTypeDef BQ25798_SetBatteryProfile(const BatteryProfile_t* profile) {
    BQ25798_SetMinSystemVoltage(profile->min_system_voltage_mV);
    BQ25798_SetChargeVoltage(profile->charge_voltage_mV);
    BQ25798_SetChargeCurrent(profile->fast_charge_current_mA);
    return bq_update_bits(REG0F_CHARGER_CTRL_0, BQ_CTRL0_EN_CHG, BQ_CTRL0_EN_CHG);
}

// ============================================================================
// OPTIMIZED TELEMETRY BURST READS
// ============================================================================

HAL_StatusTypeDef BQ25798_ReadAll(BQ25798_Data_t* data) {
    // CRITICAL FIX: These must be static so DMA doesn't corrupt the task stack!
    static uint8_t stat_fault_burst[7];
    static uint8_t adc_burst[12];

    static float ema_ibat = 0.0f;
    static float ema_ibus = 0.0f;
    static bool first_read = true;

    // Ensure ADC is running
    bq_update_bits(REG2E_ADC_CONTROL, 0x80, 0x80);

    // ==========================================
    // BURST 1: Status (7 bytes)
    // ==========================================
    if (osMutexAcquire(I2c1Mutex, 100) != osOK) return HAL_BUSY;

    // Clear any stale semaphore tokens before starting DMA
    while (osSemaphoreAcquire(I2c1DmaSemaphore, 0) == osOK);

    if (HAL_I2C_Mem_Read_DMA(bq_i2c, BQ25798_I2C_ADDR, REG1B_CHG_STAT_0, I2C_MEMADD_SIZE_8BIT, stat_fault_burst, 7) == HAL_OK) {
        if (osSemaphoreAcquire(I2c1DmaSemaphore, 100) == osOK) {
            if (bq_i2c->ErrorCode != HAL_I2C_ERROR_NONE) { // Evaluate NACK/Bus errors
                osMutexRelease(I2c1Mutex); return HAL_ERROR;
            }
        } else {
            HAL_I2C_DeInit(bq_i2c); HAL_I2C_Init(bq_i2c); // Reset peripheral on timeout
            osMutexRelease(I2c1Mutex);
            return HAL_ERROR;
        }
    } else {
        osMutexRelease(I2c1Mutex);
        return HAL_ERROR;
    }
    osMutexRelease(I2c1Mutex);

    data->stat0  = stat_fault_burst[0];
    data->stat1  = stat_fault_burst[1];
    data->fault0 = stat_fault_burst[5];
    data->fault1 = stat_fault_burst[6];

    // ==========================================
    // BURST 2: ADC Data (12 bytes)
    // ==========================================
    if (osMutexAcquire(I2c1Mutex, 100) != osOK) return HAL_BUSY;

    while (osSemaphoreAcquire(I2c1DmaSemaphore, 0) == osOK);

    if (HAL_I2C_Mem_Read_DMA(bq_i2c, BQ25798_I2C_ADDR, REG31_IBUS_ADC, I2C_MEMADD_SIZE_8BIT, adc_burst, 12) == HAL_OK) {
        if (osSemaphoreAcquire(I2c1DmaSemaphore, 100) == osOK) {
            if (bq_i2c->ErrorCode != HAL_I2C_ERROR_NONE) {
                osMutexRelease(I2c1Mutex); return HAL_ERROR;
            }
        } else {
            HAL_I2C_DeInit(bq_i2c); HAL_I2C_Init(bq_i2c);
            osMutexRelease(I2c1Mutex);
            return HAL_ERROR;
        }
    } else {
        osMutexRelease(I2c1Mutex);
        return HAL_ERROR;
    }
    osMutexRelease(I2c1Mutex);

    int16_t raw_ibus = (int16_t)((adc_burst[0] << 8) | adc_burst[1]);
    int16_t raw_ibat = (int16_t)((adc_burst[2] << 8) | adc_burst[3]);
    int16_t raw_vbus = (int16_t)((adc_burst[4] << 8) | adc_burst[5]);
    int16_t raw_vbat = (int16_t)((adc_burst[10] << 8) | adc_burst[11]);

    // ==========================================
    // READ 3: CTRL0 (Contains Backup status)
    // ==========================================
    if (osMutexAcquire(I2c1Mutex, 50) == osOK) {
        HAL_I2C_Mem_Read(bq_i2c, BQ25798_I2C_ADDR, REG0F_CHARGER_CTRL_0, I2C_MEMADD_SIZE_8BIT, &data->ctrl0, 1, 100);
        osMutexRelease(I2c1Mutex);
    }

    // --- SOFTWARE EMA FILTER ---
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

    // Parse Mode String
    const char* modes[] = {"Idle", "Trickle", "Pre-chg", "Fast", "Taper", "Res", "Top-off", "Term"};
    uint8_t m_idx = (data->stat1 >> 5) & 0x07;
    snprintf(data->status_str, sizeof(data->status_str), "%s", modes[m_idx]);

    // Override mode string if in Backup/OTG Mode
    uint8_t vbus_stat = (data->stat1 >> 1) & 0x0F;
    if (vbus_stat == 0x07 || vbus_stat == 0x0C) {
        snprintf(data->status_str, sizeof(data->status_str), "OTG");
    }

    return HAL_OK;
}

void BQ25798_GetJson(BQ25798_Data_t* data, char* buffer, size_t max_len) {
    snprintf(buffer, max_len,
        "{\"type\":\"bq\",\"vbus\":%.2f,\"vbat\":%.2f,\"ibus\":%.0f,\"ibat\":%.0f,"
        "\"pwr\":%.0f,\"vbus_pwr\":%.0f,\"stat\":\"%s\","
        "\"flt0\":%d,\"flt1\":%d,\"ctrl0\":%d}",
        data->vbus_V, data->vbat_V, data->ibus_mA, data->ibat_mA,
        data->power_mW, data->vbus_power_mW, data->status_str,
        data->fault0, data->fault1, data->ctrl0);
}

HAL_StatusTypeDef BQ25798_DumpRegisters(char* buffer, size_t max_len) {
    uint8_t regs[48];

    if (osMutexAcquire(I2c1Mutex, 100) != osOK) return HAL_BUSY;
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(bq_i2c, BQ25798_I2C_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, regs, 48, 100);
    osMutexRelease(I2c1Mutex);

    if (status != HAL_OK) return HAL_ERROR;

    int offset = snprintf(buffer, max_len, "{\"type\":\"bq_dump\",\"regs\":[");
    for (int i = 0; i < 48; i++) {
        int written = snprintf(buffer + offset, max_len - offset, "%d%s", regs[i], (i == 47) ? "" : ",");
        if (written > 0 && written < (max_len - offset)) offset += written;
        else break;
    }
    snprintf(buffer + offset, max_len - offset, "],\"rearm\":%d,\"ema\":%.2f}", bq_auto_rearm_enabled, bq_ema_coeff);
    return HAL_OK;
}

// ============================================================================
// VOLTAGE & CURRENT LIMIT SETTERS
// ============================================================================

void BQ25798_SetChargeCurrent(uint16_t current_mA) { bq_write_word(REG03_CHARGE_CURRENT, current_mA / 10); }
void BQ25798_SetInputCurrent(uint16_t current_mA)  { bq_write_word(REG06_IINDPM, current_mA / 10); }
void BQ25798_SetChargeVoltage(uint16_t voltage_mV) { bq_write_word(REG01_CHARGE_VOLTAGE, voltage_mV / 10); }
void BQ25798_SetInputVoltageLimit(uint16_t voltage_mV) { bq_write_byte(REG05_VINDPM, voltage_mV / 100); }

void BQ25798_SetTermCurrent(uint16_t current_mA) {
    uint8_t val = current_mA / 40;
    bq_update_bits(REG09_TERMINATION_CONTROL, 0x1F, val);
}

void BQ25798_SetMinSystemVoltage(uint16_t voltage_mV) {
    if(voltage_mV < 2500) voltage_mV = 2500;
    uint8_t val = (voltage_mV - 2500) / 250;
    bq_update_bits(REG00_MIN_SYS_VOLTAGE, 0x3F, val);
}

void BQ25798_SetOTGVoltage(uint16_t voltage_mV) {
    if (voltage_mV < 2800) voltage_mV = 2800;
    if (voltage_mV > 5600) voltage_mV = 5600;
    bq_write_word(REG0B_VOTG_REGULATION, (voltage_mV - 2800) / 10);
}

void BQ25798_SetOTGCurrent(uint16_t current_mA) {
    if(current_mA < 160) current_mA = 160;
    uint8_t val = (current_mA - 160) / 40;
    bq_update_bits(REG0D_IOTG_REGULATION, 0x7F, val);
}

void BQ25798_AdjustVOTG(int step_mv) {
    int16_t raw;
    if (bq_read_word(REG0B_VOTG_REGULATION, &raw) == HAL_OK) {
        uint16_t current_mv = (raw * 10) + 2800;
        int new_mv = current_mv + step_mv;
        if(new_mv < 3600) new_mv = 3600;
        if(new_mv > 12000) new_mv = 12000;
        BQ25798_SetOTGVoltage(new_mv);
    }
}

// ============================================================================
// POWER PATH CONTROL
// ============================================================================

void BQ25798_SetChargeEnable(uint8_t enable) { bq_update_bits(REG0F_CHARGER_CTRL_0, BQ_CTRL0_EN_CHG, enable ? BQ_CTRL0_EN_CHG : 0); }
void BQ25798_SetHiZ(uint8_t enable)          { bq_update_bits(REG0F_CHARGER_CTRL_0, BQ_CTRL0_EN_HIZ, enable ? BQ_CTRL0_EN_HIZ : 0); }
void BQ25798_SetOTG(uint8_t enable)          { bq_update_bits(REG12_CHARGER_CTRL_3, BQ_CTRL3_EN_OTG, enable ? BQ_CTRL3_EN_OTG : 0); }

void BQ25798_SetInputDisconnect(uint8_t disconnect) {
    bq_update_bits(REG12_CHARGER_CTRL_3, BQ_CTRL3_DIS_ACDRV, disconnect ? BQ_CTRL3_DIS_ACDRV : 0);
}

void BQ25798_SetShipMode(uint8_t mode) {
    bq_update_bits(REG11_CHARGER_CTRL_2, (3<<1), (mode & 0x03) << 1);
}

// ============================================================================
// AUTOMATED BACKUP STATE MACHINE
// ============================================================================

void BQ25798_EnableBackup5V5(void) {
    bq_write_byte(REG05_VINDPM, 4600 / 100);
    bq_update_bits(REG10_CHARGER_CTRL_1, (3 << 6), (3 << 6));
    bq_write_word(REG0B_VOTG_REGULATION, 270);

    // CHANGED: Write 0 to	 bits [4:3] to enable 3A limit AND OTG Soft-Start
    bq_update_bits(REG14_CHARGER_CTRL_5, (3 << 3), (0 << 3));

    bq_update_bits(REG0F_CHARGER_CTRL_0, BQ_CTRL0_EN_BACKUP, BQ_CTRL0_EN_BACKUP);
}

void BQ25798_SetBackup(uint8_t enable) {
    bq_update_bits(REG0F_CHARGER_CTRL_0, BQ_CTRL0_EN_BACKUP, enable ? BQ_CTRL0_EN_BACKUP : 0);
}

void BQ25798_SetBackupACFET(uint8_t enable) {
    bq_update_bits(REG16_TEMP_CONTROL, (1<<0), enable ? (1<<0) : 0);
}

void BQ25798_SetAutoRearm(uint8_t enable) { bq_auto_rearm_enabled = enable; }
void BQ25798_ToggleAutoRearm(void) { bq_auto_rearm_enabled = !bq_auto_rearm_enabled; }

void BQ25798_RearmBackup(uint16_t delay_ms) {
    if (!bq_auto_rearm_enabled) return;
    osDelay(delay_ms);

    uint8_t val;
    bq_update_bits(REG16_TEMP_CONTROL, 0x01, 0x01);
    osDelay(100);

    bool ac_valid = false;
    for (int i = 0; i < 30; i++) {
        osDelay(100);
        if (osMutexAcquire(I2c1Mutex, 50) == osOK) {
            HAL_I2C_Mem_Read(bq_i2c, BQ25798_I2C_ADDR, REG13_CHARGER_CTRL_4, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
            osMutexRelease(I2c1Mutex);
            if (val & (1 << 6)) { ac_valid = true; break; }
        }
    }

    if (ac_valid) {
        bq_update_bits(REG12_CHARGER_CTRL_3, BQ_CTRL3_EN_OTG, 0);
        osDelay(500);
        bq_update_bits(REG0F_CHARGER_CTRL_0, BQ_CTRL0_EN_BACKUP, BQ_CTRL0_EN_BACKUP);
    } else {
        bq_update_bits(REG16_TEMP_CONTROL, 0x01, 0);
    }
    osDelay(1000);
}

// ============================================================================
// ADVANCED FEATURES
// ============================================================================

void BQ25798_SetPFM_FWD(uint8_t enable) { bq_update_bits(REG12_CHARGER_CTRL_3, BQ_CTRL3_DIS_FWD_PFM, enable ? 0 : BQ_CTRL3_DIS_FWD_PFM); }
void BQ25798_SetPFM_OTG(uint8_t enable) { bq_update_bits(REG12_CHARGER_CTRL_3, BQ_CTRL3_DIS_OTG_PFM, enable ? 0 : BQ_CTRL3_DIS_OTG_PFM); }

void BQ25798_TogglePFM(void) {
    uint8_t tmp;
    if (osMutexAcquire(I2c1Mutex, 100) == osOK) {
        if (HAL_I2C_Mem_Read(bq_i2c, BQ25798_I2C_ADDR, REG12_CHARGER_CTRL_3, I2C_MEMADD_SIZE_8BIT, &tmp, 1, 100) == HAL_OK) {
            tmp ^= (BQ_CTRL3_DIS_FWD_PFM | BQ_CTRL3_DIS_OTG_PFM);
            HAL_I2C_Mem_Write(bq_i2c, BQ25798_I2C_ADDR, REG12_CHARGER_CTRL_3, I2C_MEMADD_SIZE_8BIT, &tmp, 1, 100);
        }
        osMutexRelease(I2c1Mutex);
    }
}

void BQ25798_SetWatchdog(uint8_t enable) { bq_update_bits(REG10_CHARGER_CTRL_1, BQ_CTRL1_WD_MASK, enable ? 0x05 : 0x00); }

void BQ25798_ForceDetection(void) {
    bq_update_bits(REG11_CHARGER_CTRL_2, BQ_CTRL2_FORCE_INDET, BQ_CTRL2_FORCE_INDET);
    bq_update_bits(REG0F_CHARGER_CTRL_0, (1<<3), (1<<3));
}

void BQ25798_SetADCResolution(uint8_t bits) {
    uint8_t val = BQ_ADC_RES_15BIT; // Default

    if (bits == 14)      val = BQ_ADC_RES_14BIT;
    else if (bits == 13) val = BQ_ADC_RES_13BIT;
    else if (bits == 12) val = BQ_ADC_RES_12BIT;

    bq_update_bits(REG2E_ADC_CONTROL, BQ_ADC_RESOLUTION_MASK, val);
}

void BQ25798_SetEMA(float coeff) {
    if (coeff < 0.1f) coeff = 0.1f;
    if (coeff > 1.0f) coeff = 1.0f;
    bq_ema_coeff = coeff;
}
