/**
 * @file mailbox.c
 * @brief VideoCore Mailbox Interface for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * The mailbox is used to communicate with the VideoCore GPU.
 * Property tags are used to configure the framebuffer, get hardware
 * info, and control various system functions.
 */

#include "mailbox.h"
#include "rpi3_hw.h"

/**
 * @brief Call the VideoCore mailbox with a property buffer
 *
 * The buffer must be 16-byte aligned. The buffer format is:
 *   [0] = total size in bytes
 *   [1] = request code (0 for request)
 *   [2] = tag ID
 *   [3] = value buffer size
 *   [4] = request/response indicator (0 for request)
 *   [5+] = value buffer
 *   ... more tags ...
 *   [N] = end tag (0)
 *
 * @param buffer 16-byte aligned buffer with property tags
 * @return 0 on success, -1 on failure
 */
int mbox_call(volatile uint32_t *buffer)
{
    /* Ensure 16-byte alignment */
    uint32_t addr = (uint32_t)(uintptr_t)buffer;
    if (addr & 0xF) {
        return -1;  /* Buffer not aligned */
    }

    /* Data synchronization barrier */
    __asm__ volatile("dsb sy" ::: "memory");

    /* Use direct ARM address - RPi3 VC sees same physical address space */
    uint32_t msg = (addr & ~0xF) | MBOX_CH_PROP;

    /* Wait for mailbox to be ready (not full) with timeout */
    uint32_t timeout = 1000000;
    while ((mmio_read32(MBOX_STATUS) & MBOX_FULL) && timeout > 0) {
        __asm__ volatile("nop");
        timeout--;
    }
    if (timeout == 0) {
        return -2;  /* Timeout waiting for mailbox ready */
    }

    /* Write to mailbox */
    mmio_write32(MBOX_WRITE, msg);

    /* Wait for response with timeout */
    timeout = 1000000;
    while (timeout > 0) {
        /* Wait for response (not empty) */
        while ((mmio_read32(MBOX_STATUS) & MBOX_EMPTY) && timeout > 0) {
            __asm__ volatile("nop");
            timeout--;
        }
        if (timeout == 0) {
            return -3;  /* Timeout waiting for response */
        }

        /* Read response */
        uint32_t resp = mmio_read32(MBOX_READ);

        /* Check if this is our response (same channel) */
        if ((resp & 0xF) == MBOX_CH_PROP) {
            break;
        }
        timeout--;
    }
    if (timeout == 0) {
        return -4;  /* Timeout waiting for correct channel */
    }

    /* Memory barrier to see GPU's response */
    __asm__ volatile("dsb sy" ::: "memory");

    /* Check response code */
    if (buffer[1] != MBOX_RESPONSE_SUCCESS) {
        return -1;
    }

    return 0;
}

/**
 * @brief Get SoC temperature via mailbox
 *
 * @return Temperature in millicelsius, or -1 on error
 */
int32_t mbox_get_temperature(void)
{
    /* Buffer must be 16-byte aligned */
    volatile uint32_t __attribute__((aligned(16))) buffer[8];

    buffer[0] = 8 * 4;              /* Total size */
    buffer[1] = MBOX_REQUEST;       /* Request */
    buffer[2] = MBOX_TAG_GET_TEMP;  /* Tag: get temperature */
    buffer[3] = 8;                  /* Value buffer size */
    buffer[4] = 0;                  /* Request indicator */
    buffer[5] = MBOX_TEMP_ID_SOC;   /* Temperature ID (0 = SoC) */
    buffer[6] = 0;                  /* Response value placeholder */
    buffer[7] = MBOX_TAG_END;       /* End tag */

    if (mbox_call(buffer) != 0) {
        return -1;
    }

    /* Temperature is returned in buffer[6] in millicelsius */
    return (int32_t)buffer[6];
}

/**
 * @brief Get maximum safe SoC temperature via mailbox
 *
 * @return Max temperature in millicelsius, or -1 on error
 */
int32_t mbox_get_max_temperature(void)
{
    volatile uint32_t __attribute__((aligned(16))) buffer[8];

    buffer[0] = 8 * 4;
    buffer[1] = MBOX_REQUEST;
    buffer[2] = MBOX_TAG_GET_MAX_TEMP;
    buffer[3] = 8;
    buffer[4] = 0;
    buffer[5] = MBOX_TEMP_ID_SOC;
    buffer[6] = 0;
    buffer[7] = MBOX_TAG_END;

    if (mbox_call(buffer) != 0) {
        return -1;
    }

    return (int32_t)buffer[6];
}

/**
 * @brief Get ARM clock rate via mailbox
 *
 * @return Clock rate in Hz, or 0 on error
 */
uint32_t mbox_get_arm_clock(void)
{
    volatile uint32_t __attribute__((aligned(16))) buffer[8];

    buffer[0] = 8 * 4;
    buffer[1] = MBOX_REQUEST;
    buffer[2] = MBOX_TAG_GET_CLOCK_RATE;
    buffer[3] = 8;
    buffer[4] = 0;
    buffer[5] = MBOX_CLOCK_ID_ARM;
    buffer[6] = 0;
    buffer[7] = MBOX_TAG_END;

    if (mbox_call(buffer) != 0) {
        return 0;
    }

    return buffer[6];
}

/**
 * @brief Set device power state via mailbox
 *
 * @param device_id Device ID (MBOX_DEVICE_*)
 * @param on 1 to power on, 0 to power off
 * @return 0 on success, -1 on error
 */
int mbox_set_power(uint32_t device_id, int on)
{
    volatile uint32_t __attribute__((aligned(16))) buffer[8];

    buffer[0] = 8 * 4;              /* Total size */
    buffer[1] = MBOX_REQUEST;       /* Request */
    buffer[2] = MBOX_TAG_SET_POWER_STATE;  /* Tag: set power state */
    buffer[3] = 8;                  /* Value buffer size */
    buffer[4] = 0;                  /* Request indicator */
    buffer[5] = device_id;          /* Device ID */
    buffer[6] = (on ? MBOX_POWER_STATE_ON : MBOX_POWER_STATE_OFF) | MBOX_POWER_STATE_WAIT;
    buffer[7] = MBOX_TAG_END;       /* End tag */

    if (mbox_call(buffer) != 0) {
        return -1;
    }

    /* Check if device powered on successfully */
    /* Response: bit 0 = on, bit 1 = device exists */
    if (on && !(buffer[6] & MBOX_POWER_STATE_ON)) {
        return -1;  /* Failed to power on */
    }

    return 0;
}

/**
 * @brief Get device power state via mailbox
 *
 * @param device_id Device ID (MBOX_DEVICE_*)
 * @return Power state (bit 0 = on, bit 1 = exists), or -1 on error
 */
int mbox_get_power(uint32_t device_id)
{
    volatile uint32_t __attribute__((aligned(16))) buffer[8];

    buffer[0] = 8 * 4;
    buffer[1] = MBOX_REQUEST;
    buffer[2] = MBOX_TAG_GET_POWER_STATE;
    buffer[3] = 8;
    buffer[4] = 0;
    buffer[5] = device_id;
    buffer[6] = 0;
    buffer[7] = MBOX_TAG_END;

    if (mbox_call(buffer) != 0) {
        return -1;
    }

    return (int)buffer[6];
}
