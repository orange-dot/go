/**
 * @file mailbox.h
 * @brief VideoCore Mailbox Interface for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_MAILBOX_H
#define RPI3_MAILBOX_H

#include <stdint.h>

/* Mailbox request/response codes */
#define MBOX_REQUEST            0x00000000
#define MBOX_RESPONSE_SUCCESS   0x80000000
#define MBOX_RESPONSE_ERROR     0x80000001

/* Property tags for framebuffer */
#define MBOX_TAG_SET_PHYS_WH    0x00048003  /* Set physical (display) width/height */
#define MBOX_TAG_SET_VIRT_WH    0x00048004  /* Set virtual (buffer) width/height */
#define MBOX_TAG_SET_DEPTH      0x00048005  /* Set bits per pixel */
#define MBOX_TAG_SET_PIXEL_ORDER 0x00048006 /* Set pixel order (RGB/BGR) */
#define MBOX_TAG_ALLOC_FB       0x00040001  /* Allocate framebuffer */
#define MBOX_TAG_GET_PITCH      0x00040008  /* Get bytes per row */
#define MBOX_TAG_END            0x00000000  /* End tag */

/* Property tags for hardware info */
#define MBOX_TAG_GET_TEMP       0x00030006  /* Get temperature (in millicelsius) */
#define MBOX_TAG_GET_MAX_TEMP   0x0003000A  /* Get max temperature */
#define MBOX_TAG_GET_VOLTAGE    0x00030003  /* Get voltage */
#define MBOX_TAG_GET_CLOCK_RATE 0x00030002  /* Get clock rate */

/* Temperature/voltage IDs */
#define MBOX_TEMP_ID_SOC        0           /* SoC temperature */
#define MBOX_VOLTAGE_ID_CORE    1           /* Core voltage */
#define MBOX_CLOCK_ID_ARM       3           /* ARM clock */

/* Pixel order values */
#define MBOX_PIXEL_ORDER_BGR    0
#define MBOX_PIXEL_ORDER_RGB    1

/* Property tags for power control */
#define MBOX_TAG_GET_POWER_STATE  0x00020001  /* Get power state */
#define MBOX_TAG_SET_POWER_STATE  0x00028001  /* Set power state */

/* Device IDs for power control */
#define MBOX_DEVICE_SD          0           /* SD card */
#define MBOX_DEVICE_UART0       1           /* UART0 */
#define MBOX_DEVICE_UART1       2           /* UART1 */
#define MBOX_DEVICE_USB_HCD     3           /* USB Host Controller */
#define MBOX_DEVICE_I2C0        4           /* I2C0 */
#define MBOX_DEVICE_I2C1        5           /* I2C1 */
#define MBOX_DEVICE_I2C2        6           /* I2C2 */
#define MBOX_DEVICE_SPI         7           /* SPI */
#define MBOX_DEVICE_CCP2TX      8           /* CCP2TX */

/* Power state bits */
#define MBOX_POWER_STATE_OFF      0
#define MBOX_POWER_STATE_ON       1
#define MBOX_POWER_STATE_WAIT     (1 << 1)  /* Wait for device ready */
#define MBOX_POWER_STATE_EXISTS   (1 << 1)  /* Device exists (response) */

/**
 * @brief Call the VideoCore mailbox with a property buffer
 *
 * @param buffer 16-byte aligned buffer with property tags
 * @return 0 on success, -1 on failure
 */
int mbox_call(volatile uint32_t *buffer);

/**
 * @brief Get SoC temperature
 *
 * @return Temperature in millicelsius (e.g., 45000 = 45.0°C), or -1 on error
 */
int32_t mbox_get_temperature(void);

/**
 * @brief Get maximum safe SoC temperature
 *
 * @return Max temperature in millicelsius, or -1 on error
 */
int32_t mbox_get_max_temperature(void);

/**
 * @brief Get ARM clock rate
 *
 * @return Clock rate in Hz, or 0 on error
 */
uint32_t mbox_get_arm_clock(void);

/**
 * @brief Set device power state
 *
 * @param device_id Device ID (MBOX_DEVICE_*)
 * @param on 1 to power on, 0 to power off
 * @return 0 on success, -1 on error
 */
int mbox_set_power(uint32_t device_id, int on);

/**
 * @brief Get device power state
 *
 * @param device_id Device ID (MBOX_DEVICE_*)
 * @return Power state (bit 0 = on, bit 1 = exists), or -1 on error
 */
int mbox_get_power(uint32_t device_id);

#endif /* RPI3_MAILBOX_H */
