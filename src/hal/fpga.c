#include "hal/fpga.h"
#include "system_config.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>


/* Defines the TX Queue referenced by frontend_api.c */
K_MSGQ_DEFINE(fpga_tx_queue, sizeof(FpgaSpiPacket_t), FPGA_TX_QUEUE_SIZE, FPGA_TX_QUEUE_ALIGN);

uint8_t  current_slot = 0;
uint8_t  fpga_is_ready = 0;
char     slot_names[FPGA_MAX_SLOTS][FPGA_SLOT_NAME_LEN] = {0};
uint8_t  slot_clk_configs[FPGA_MAX_SLOTS] = {0};
uint8_t  last_active_slot = 0;


static const struct spi_dt_spec fpga_spi = SPI_DT_SPEC_GET(DT_NODELABEL(fpga_spi), SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE, 0);
static const struct gpio_dt_spec fpga_rst = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_rst), gpios);
static const struct gpio_dt_spec fpga_ndone = GPIO_DT_SPEC_GET(DT_NODELABEL(fpga_ndone), gpios);

static uint8_t fpga_tx_buf[FPGA_TX_BUF_SIZE];
static K_MUTEX_DEFINE(fpga_flash_mutex);

/** @brief Stubs for saving dynamic configurations to non-volatile memory. */
void Save_Configs(void) {}

/** @brief Stubs for dynamically adjusting hardware clock routing per slot. */
void Apply_Slot_Clock(uint8_t slot) {}

void FPGA_PowerDown(void) {
    if (gpio_is_ready_dt(&fpga_rst)) gpio_pin_configure_dt(&fpga_rst, GPIO_OUTPUT_INACTIVE);
}

void FPGA_RequestPowerUp(void) { printk("LOG: FPGA Power-up Sequence Triggered\n"); }

int FPGA_Program_Slot(uint8_t slot) {
    if (!spi_is_ready_dt(&fpga_spi) || !gpio_is_ready_dt(&fpga_rst)) return -ENODEV;

    /* Lock to ensure static buffer isn't polluted by concurrent calls */
    k_mutex_lock(&fpga_flash_mutex, K_FOREVER);

    gpio_pin_configure_dt(&fpga_rst, GPIO_OUTPUT_INACTIVE); // Assert Low
    k_msleep(FPGA_RESET_ASSERT_MS);
    gpio_pin_set_dt(&fpga_rst, 1); // Release to High
    k_msleep(FPGA_RESET_RELEASE_MS);

    char path[SD_MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/slot%d.bin", SD_MOUNT_POINT, slot);

    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, path, FS_O_READ) != 0) {
        printk("ERR: FPGA bitstream '%s' not found\n", path);
        k_mutex_unlock(&fpga_flash_mutex);
        return -ENOENT;
    }

    ssize_t br;
    struct spi_buf tx_buf = { .buf = fpga_tx_buf };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

    while ((br = fs_read(&file, fpga_tx_buf, sizeof(fpga_tx_buf))) > 0) {
        tx_buf.len = br;
        spi_write_dt(&fpga_spi, &tx_bufs);
    }
    fs_close(&file);

    /* Push dummy clocks */
    memset(fpga_tx_buf, 0xFF, 16);
    tx_buf.len = 16;
    spi_write_dt(&fpga_spi, &tx_bufs);
    k_msleep(5);

    k_mutex_unlock(&fpga_flash_mutex);

    if (gpio_pin_get_dt(&fpga_ndone) == 1) {
        printk(">>> SUCCESS: FPGA RUNNING <<<\n");
        fpga_is_ready = 1;
        return 0;
    } else {
        printk("ERR: FPGA nDONE low\n");
        fpga_is_ready = 0;
        return -EIO;
    }
}

/** @brief Thread-safe ingestion of SPI commands from the application layer. */
int FPGA_EnqueueSpiPacket(const FpgaSpiPacket_t *packet) {
    if (!packet) return -EINVAL;
    return k_msgq_put(&fpga_tx_queue, packet, K_NO_WAIT);
}