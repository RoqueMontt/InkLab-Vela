#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "system_config.h"
#include "proto/frontend_api.h"
#include "rtos/usb_task.h"

typedef struct {
    char data[USB_MSG_MAX_LEN];
} UsbMsg_t;

/* Zephyr Message Queue (No ring buffer needed for polling!) */
K_MSGQ_DEFINE(usb_tx_q, sizeof(UsbMsg_t), USB_TX_QUEUE_SIZE, __alignof__(UsbMsg_t));

static const struct device *cdc_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

/** 
 * @brief Thread-safe Printf redirect.
 */
void USB_Printf(const char *format, ...) {
    UsbMsg_t msg;
    va_list args;
    va_start(args, format);
    vsnprintf(msg.data, sizeof(msg.data), format, args);
    va_end(args);
    k_msgq_put(&usb_tx_q, &msg, K_NO_WAIT);
}

/** 
 * @brief Official Zephyr Printk Override.
 * CRITICAL FIX: irq_lock() prevents the HardFault caused by multiple 
 * threads calling printk() at the exact same millisecond.
 */
int arch_printk_char_out(int c) {
    static char buf[USB_MSG_MAX_LEN];
    static int idx = 0;
    
    /* Lock system interrupts to safely interact with the static buffer */
    unsigned int key = irq_lock();
    
    buf[idx++] = (char)c;
    if (c == '\n' || idx >= (sizeof(buf) - 1)) {
        buf[idx] = '\0';
        UsbMsg_t msg;
        /* memcpy is much safer and faster than strncpy inside an IRQ lock */
        memcpy(msg.data, buf, idx + 1);
        k_msgq_put(&usb_tx_q, &msg, K_NO_WAIT);
        idx = 0;
    }
    
    irq_unlock(key);
    return c;
}

/** 
 * @brief Core USB communication thread routine.
 * CRITICAL FIX: Uses non-blocking polling. No IRQ storms, no lockups.
 */
void usb_task_fn(void *arg1, void *arg2, void *arg3) {
    if (!device_is_ready(cdc_dev)) {
        return;
    }

    Frontend_Init();
    UsbMsg_t tx_msg;

    while (1) {
        /* 1. Check if terminal is actually connected (DTR flag) */
        uint32_t dtr = 0;
        uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);

        /* 2. Process Outgoing Data */
        if (dtr) {
            /* Send up to 5 messages per loop to prevent blocking RX processing */
            int tx_count = 0;
            while (tx_count < 5 && k_msgq_get(&usb_tx_q, &tx_msg, K_NO_WAIT) == 0) {
                for (int i = 0; tx_msg.data[i] != '\0'; i++) {
                    uart_poll_out(cdc_dev, tx_msg.data[i]);
                }
                tx_count++;
            }
        } else {
            /* If no terminal is open, drain the queue into the void so RAM doesn't overflow */
            while (k_msgq_get(&usb_tx_q, &tx_msg, K_NO_WAIT) == 0) {}
        }

        /* 3. Process Incoming Data (Non-blocking Poll) */
        uint8_t chunk[UART_RX_CHUNK_SIZE];
        int rx_count = 0;
        unsigned char c;
        
        /* Read until the internal USB CDC buffer is empty, up to our chunk size */
        while (rx_count < UART_RX_CHUNK_SIZE && uart_poll_in(cdc_dev, &c) == 0) {
            chunk[rx_count++] = c;
        }

        if (rx_count > 0) {
            Frontend_ProcessBlock(chunk, rx_count);
        }

        Frontend_CheckTimeout();

        /* Yield to system scheduler (1ms is standard and highly efficient for USB polling) */
        k_msleep(1);
    }
}

K_THREAD_DEFINE(usb_thread, USB_THREAD_STACK_SIZE, usb_task_fn, NULL, NULL, NULL, USB_THREAD_PRIORITY, 0, 0);