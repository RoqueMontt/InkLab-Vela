/**
 * @file usb_task.h
 * @brief Public interface for the USB CDC telemetry task.
 */
#ifndef USB_TASK_H
#define USB_TASK_H

/**
 * @brief Send a formatted string over the USB CDC ACM connection.
 * 
 * @param format Standard printf format string.
 * @param ... Variable arguments.
 * 
 * @note This is thread-safe and non-blocking. Safe to call from any thread.
 */
void USB_Printf(const char *format, ...);

#endif /* USB_TASK_H */