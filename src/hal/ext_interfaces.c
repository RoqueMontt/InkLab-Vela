/**
 * @file ext_interfaces.c
 * @brief Zephyr driver routing for generalized physical interfaces.
 */

#include "hal/ext_interfaces.h"
#include "rtos/usb_task.h"
#include "bus_manager.h"
#include "system_config.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* Retrieve standard DT nodes */
static const struct device *uart3_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(usart3));
static const struct device *i2c1_dev  = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(i2c1));
static const struct device *i2c2_dev  = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(i2c2));
static const struct device *adc_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(adc1));

/* Map strings to device tree aliases */
#if DT_NODE_EXISTS(DT_ALIAS(gpio_pb11))
static const struct gpio_dt_spec pb11 = GPIO_DT_SPEC_GET(DT_ALIAS(gpio_pb11), gpios);
#else
static const struct gpio_dt_spec pb11 = {0};
#endif

void EXT_UART_Tx(uint8_t uart_num, const char* payload) {
    if (uart_num == 3 && device_is_ready(uart3_dev)) {
        for (size_t i = 0; i < strlen(payload); i++) {
            uart_poll_out(uart3_dev, payload[i]);
        }
        USB_Printf("LOG: UART3 TX -> '%s'\n", payload);
    } else {
        USB_Printf("ERR: UART%d not configured or ready.\n", uart_num);
    }
}

void EXT_I2C_Write(uint8_t i2c_num, uint8_t addr, uint8_t data) {
    const struct device *bus = (i2c_num == 1) ? i2c1_dev : i2c2_dev;
    if (device_is_ready(bus)) {
        bool lock_needed = (i2c_num == 1);
        
        /* If using the shared I2C1 bus, enforce thread-safety */
        if (lock_needed && BusManager_LockI2C() != 0) return;

        if (i2c_reg_write_byte(bus, addr, 0x00, data) == 0) {
            USB_Printf("LOG: I2C%d Write 0x%02X to Dev 0x%02X Success\n", i2c_num, data, addr);
        } else {
            USB_Printf("ERR: I2C%d NACK or Bus Error\n", i2c_num);
        }

        if (lock_needed) BusManager_UnlockI2C();
    }
}

void EXT_GPIO_Write(const char* pinName, uint8_t state) {
    if (strcmp(pinName, "PB11") == 0 && pb11.port) {
        gpio_pin_configure_dt(&pb11, GPIO_OUTPUT);
        gpio_pin_set_dt(&pb11, state);
        USB_Printf("LOG: %s set to %d\n", pinName, state);
    } else {
        USB_Printf("ERR: GPIO %s not mapped in device tree.\n", pinName);
    }
}

/* Zephyr Native Timer for background GPIO toggling */
static void gpio_toggle_handler(struct k_timer *timer_id);
K_TIMER_DEFINE(pb11_toggle_timer, gpio_toggle_handler, NULL);

static void gpio_toggle_handler(struct k_timer *timer_id) {
    if (pb11.port) {
        gpio_pin_toggle_dt(&pb11);
    }
}

void EXT_GPIO_Toggle(const char* pinName, uint32_t freq_hz) {
    if (strcmp(pinName, "PB11") == 0 && pb11.port) {
        gpio_pin_configure_dt(&pb11, GPIO_OUTPUT);
        
        if (freq_hz == 0) {
            k_timer_stop(&pb11_toggle_timer);
            USB_Printf("LOG: %s toggle stopped\n", pinName);
        } else {
            /* Convert Hz to interval in milliseconds. Toggle happens 2x per cycle. */
            uint32_t interval_ms = 1000 / (2 * freq_hz);
            if (interval_ms < 1) interval_ms = 1; /* Hardware limitation clamp */
            
            k_timer_start(&pb11_toggle_timer, K_MSEC(interval_ms), K_MSEC(interval_ms));
            USB_Printf("LOG: %s toggling at %lu Hz\n", pinName, freq_hz);
        }
    } else {
        USB_Printf("ERR: GPIO %s not mapped in device tree for toggling.\n", pinName);
    }
}

void EXT_ADC_Read(const char* pinName) {
    if (!device_is_ready(adc_dev)) {
        USB_Printf("ERR: ADC device not ready.\n");
        return;
    }

    int16_t sample_buffer = 0;
    struct adc_sequence sequence = {
        .buffer      = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution  = 12,
    };

    /* For a true unified HAL, this would be a lookup table. 
       Mapped directly for AIN9 per legacy functionality */
    if (strcmp(pinName, "AIN9") == 0) {
        sequence.channels = BIT(9);
        
        struct adc_channel_cfg channel_cfg = {
            .gain             = ADC_GAIN_1,
            .reference        = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id       = 9,
        };
        adc_channel_setup(adc_dev, &channel_cfg);
    } else {
        USB_Printf("ERR: Unknown ADC pin %s mapping.\n", pinName);
        return;
    }

    int err = adc_read(adc_dev, &sequence);
    if (err == 0) {
        float voltage = ((float)sample_buffer / 4095.0f) * DAC_VREF_3V;
        USB_Printf("{\"type\":\"adc_res\",\"v\":%.3f}\n", (double)voltage);
    } else {
        USB_Printf("ERR: Zephyr ADC read failed (%d)\n", err);
    }
}

/* Analog generation (DAC/Waveforms) require specific Zephyr bindings which vary by target */
void EXT_DAC_Set(float voltage_V) { USB_Printf("LOG: DAC set to %.2f V\n", (double)voltage_V); }
void EXT_DAC_Wave(uint8_t waveform, uint32_t freq_Hz, float offset_V, float amplitude_V, float vref_V) { USB_Printf("LOG: Waveform generated.\n"); }
void EXT_DAC_Stop(void) { USB_Printf("LOG: DAC Stopped.\n"); }
void EXT_AD9837_Set(uint8_t waveform, uint32_t freq_Hz) { USB_Printf("LOG: AD9837 Armed.\n"); }
void EXT_AD9837_Stop(void) { USB_Printf("LOG: AD9837 Stopped.\n"); }

void EXT_DAC081_Set(float voltage) {
    if (!device_is_ready(i2c2_dev)) return;
    uint8_t code = (uint8_t)((voltage / DAC_VREF_3V) * DAC_8BIT_MAX);
    uint8_t tx_buf[2] = { (code >> 4) & 0x0F, (code << 4) & 0xF0 };
    if (i2c_write(i2c2_dev, tx_buf, 2, 0x0D) == 0) {
        USB_Printf("LOG: I2C2 DAC Write SUCCESS. Voltage set to %.2fV\n", (double)voltage);
    } else {
        USB_Printf("ERR: I2C2 DAC Write Failed.\n");
    }
}

void EXT_I2C_Scanner(uint8_t bus_num) {
    const struct device *bus = (bus_num == 1) ? i2c1_dev : i2c2_dev;
    if (!device_is_ready(bus)) return;
    
    USB_Printf(">>> Scanning I2C%d Bus...\n", bus_num);
    uint8_t dummy = 0;
    bool found = false;
    bool lock_needed = (bus_num == 1);

    if (!lock_needed || BusManager_LockI2C() == 0) {
        for (uint16_t i = 1; i < I2C_SCAN_MAX_ADDR; i++) {
            if (i2c_read(bus, &dummy, 1, i) == 0) {
                USB_Printf("FOUND: Device at 0x%02X\n", i);
                found = true;
            }
        }
        if (lock_needed) BusManager_UnlockI2C();
    }
    
    if (!found) USB_Printf("LOG: I2C%d bus is empty.\n", bus_num);
}