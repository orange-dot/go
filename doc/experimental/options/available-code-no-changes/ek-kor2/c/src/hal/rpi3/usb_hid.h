/**
 * @file usb_hid.h
 * @brief USB HID Keyboard Driver Interface
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements USB HID boot protocol keyboard support for bare-metal use.
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>

/**
 * @brief Initialize HID keyboard driver
 *
 * @param addr USB device address
 * @param ep Endpoint address (with direction bit)
 * @param interval Polling interval in ms
 */
void usb_hid_init(uint8_t addr, uint8_t ep, uint8_t interval);

/**
 * @brief Poll HID keyboard for new keystrokes
 *
 * Call this periodically from the main loop or USB poll routine.
 */
void usb_hid_poll(void);

/**
 * @brief Check if characters are available in keyboard buffer
 *
 * @return Number of characters available
 */
int usb_hid_available(void);

/**
 * @brief Get next character from keyboard buffer
 *
 * @return Character, or -1 if buffer empty
 */
int usb_hid_getchar(void);

/**
 * @brief Check if Caps Lock is active
 *
 * @return 1 if Caps Lock on, 0 otherwise
 */
int usb_hid_caps_lock(void);

/**
 * @brief Check if Num Lock is active
 *
 * @return 1 if Num Lock on, 0 otherwise
 */
int usb_hid_num_lock(void);

#endif /* USB_HID_H */
