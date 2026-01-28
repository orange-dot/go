/**
 * @file uart.c
 * @brief Mini-UART Driver for Raspberry Pi 3B+ (BCM2837B0)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Uses the Mini-UART (auxiliary UART) on GPIO 14/15.
 * Configured for 115200 baud, 8N1.
 *
 * Note: The Mini-UART baudrate is derived from the VPU clock,
 * which is typically 250 MHz on Pi 3B+.
 */

#include "rpi3_hw.h"
#include "uart.h"
#include <stdarg.h>

/* ============================================================================
 * Private Constants
 * ============================================================================ */

/* VPU clock frequency (typically 250 MHz on Pi 3B+) */
#define VPU_CLOCK_HZ        250000000

/* Target baud rate */
#define BAUD_RATE           115200

/* Baud rate divisor: baudrate = system_clock / (8 * (baud_reg + 1)) */
/* 115200 = 250000000 / (8 * (270 + 1)) = 250000000 / 2168 = 115313 (~0.1% error) */
#define BAUD_DIVISOR        ((VPU_CLOCK_HZ / (8 * BAUD_RATE)) - 1)

/* ============================================================================
 * GPIO Configuration
 * ============================================================================ */

/**
 * @brief Set GPIO pin function
 */
static void gpio_set_function(uint32_t pin, uint32_t func)
{
    uint32_t reg_offset = (pin / 10) * 4;
    uint32_t bit_offset = (pin % 10) * 3;
    uintptr_t reg_addr = GPIO_BASE + reg_offset;

    uint32_t val = mmio_read32(reg_addr);
    val &= ~(7 << bit_offset);          /* Clear function bits */
    val |= (func << bit_offset);        /* Set new function */
    mmio_write32(reg_addr, val);
}

/**
 * @brief Set GPIO pull-up/down
 */
static void gpio_set_pull(uint32_t pin, uint32_t pull)
{
    /* BCM2837 pull-up/down sequence:
     * 1. Write to GPPUD to set desired state
     * 2. Wait 150 cycles
     * 3. Write to GPPUDCLK to clock in the state
     * 4. Wait 150 cycles
     * 5. Clear GPPUD
     * 6. Clear GPPUDCLK
     */
    mmio_write32(GPPUD, pull);
    delay_cycles(150);

    uint32_t clk_reg = (pin < 32) ? GPPUDCLK0 : GPPUDCLK1;
    uint32_t bit = (pin < 32) ? pin : (pin - 32);
    mmio_write32(clk_reg, 1 << bit);
    delay_cycles(150);

    mmio_write32(GPPUD, 0);
    mmio_write32(clk_reg, 0);
}

/* ============================================================================
 * UART Functions
 * ============================================================================ */

/**
 * @brief Initialize Mini-UART
 */
void uart_init(void)
{
    /* Enable Mini-UART */
    mmio_write32(AUX_ENABLES, mmio_read32(AUX_ENABLES) | AUX_ENABLE_MINIUART);

    /* Disable TX/RX during configuration */
    mmio_write32(AUX_MU_CNTL, 0);

    /* Disable interrupts */
    mmio_write32(AUX_MU_IER, 0);

    /* Set 8-bit mode */
    mmio_write32(AUX_MU_LCR, AUX_MU_LCR_8BIT);

    /* Clear modem control */
    mmio_write32(AUX_MU_MCR, 0);

    /* Set baud rate */
    mmio_write32(AUX_MU_BAUD, BAUD_DIVISOR);

    /* Clear FIFOs */
    mmio_write32(AUX_MU_IIR, 0xC6);

    /* Configure GPIO 14 (TX) and GPIO 15 (RX) for ALT5 (Mini-UART) */
    gpio_set_function(14, GPIO_FUNC_ALT5);
    gpio_set_function(15, GPIO_FUNC_ALT5);

    /* Disable pull-up/down on TX/RX pins */
    gpio_set_pull(14, 0);
    gpio_set_pull(15, 0);

    /* Enable TX and RX */
    mmio_write32(AUX_MU_CNTL, 3);
}

/**
 * @brief Check if transmit FIFO has space
 */
int uart_tx_ready(void)
{
    return (mmio_read32(AUX_MU_LSR) & AUX_MU_LSR_TX_EMPTY) != 0;
}

/**
 * @brief Check if receive FIFO has data
 */
int uart_rx_ready(void)
{
    return (mmio_read32(AUX_MU_LSR) & AUX_MU_LSR_DATA_READY) != 0;
}

/**
 * @brief Send a single character (blocking)
 */
void uart_putchar(char c)
{
    /* Wait for transmit FIFO to have space */
    while (!uart_tx_ready()) {
        __asm__ volatile("yield");
    }
    mmio_write32(AUX_MU_IO, (uint32_t)c);
}

/**
 * @brief Receive a single character (blocking)
 */
char uart_getchar(void)
{
    /* Wait for receive FIFO to have data */
    while (!uart_rx_ready()) {
        __asm__ volatile("yield");
    }
    return (char)(mmio_read32(AUX_MU_IO) & 0xFF);
}

/**
 * @brief Send a null-terminated string
 */
void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            uart_putchar('\r');     /* Add CR before LF */
        }
        uart_putchar(*s++);
    }
}

/**
 * @brief Send a buffer of bytes
 */
void uart_write(const char *buf, uint32_t len)
{
    while (len--) {
        uart_putchar(*buf++);
    }
}

/* ============================================================================
 * Printf Implementation (minimal)
 * ============================================================================ */

/**
 * @brief Print unsigned integer in given base
 */
static void uart_print_uint(uint64_t value, int base, int width, char pad)
{
    char buf[20];
    int i = 0;

    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            int digit = value % base;
            buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            value /= base;
        }
    }

    /* Pad if needed */
    while (i < width) {
        uart_putchar(pad);
        width--;
    }

    /* Print in reverse */
    while (i > 0) {
        uart_putchar(buf[--i]);
    }
}

/**
 * @brief Print signed integer
 */
static void uart_print_int(int64_t value, int width, char pad)
{
    if (value < 0) {
        uart_putchar('-');
        value = -value;
        if (width > 0) width--;
    }
    uart_print_uint((uint64_t)value, 10, width, pad);
}

/**
 * @brief Minimal printf implementation
 */
void uart_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            /* Parse width and padding */
            char pad = ' ';
            int width = 0;

            if (*fmt == '0') {
                pad = '0';
                fmt++;
            }

            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            /* Handle length modifiers */
            int is_long = 0;
            int is_longlong = 0;
            if (*fmt == 'l') {
                is_long = 1;
                fmt++;
                if (*fmt == 'l') {
                    is_longlong = 1;
                    fmt++;
                }
            }

            /* Handle format specifiers */
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int64_t val;
                    if (is_longlong) {
                        val = va_arg(args, int64_t);
                    } else if (is_long) {
                        val = va_arg(args, long);
                    } else {
                        val = va_arg(args, int);
                    }
                    uart_print_int(val, width, pad);
                    break;
                }
                case 'u': {
                    uint64_t val;
                    if (is_longlong) {
                        val = va_arg(args, uint64_t);
                    } else if (is_long) {
                        val = va_arg(args, unsigned long);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    uart_print_uint(val, 10, width, pad);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (is_longlong) {
                        val = va_arg(args, uint64_t);
                    } else if (is_long) {
                        val = va_arg(args, unsigned long);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    uart_print_uint(val, 16, width, pad);
                    break;
                }
                case 'p': {
                    uart_puts("0x");
                    uart_print_uint((uint64_t)(uintptr_t)va_arg(args, void *), 16, 16, '0');
                    break;
                }
                case 's': {
                    const char *s = va_arg(args, const char *);
                    if (s) {
                        uart_puts(s);
                    } else {
                        uart_puts("(null)");
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    uart_putchar(c);
                    break;
                }
                case '%': {
                    uart_putchar('%');
                    break;
                }
                default: {
                    uart_putchar('%');
                    uart_putchar(*fmt);
                    break;
                }
            }
        } else {
            if (*fmt == '\n') {
                uart_putchar('\r');
            }
            uart_putchar(*fmt);
        }
        fmt++;
    }

    va_end(args);
}
