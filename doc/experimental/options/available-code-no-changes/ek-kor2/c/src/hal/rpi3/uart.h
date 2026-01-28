/**
 * @file uart.h
 * @brief Mini-UART Driver Header for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_UART_H
#define RPI3_UART_H

#include <stdint.h>

/**
 * @brief Initialize Mini-UART (115200 baud, 8N1)
 */
void uart_init(void);

/**
 * @brief Check if transmit FIFO has space
 * @return Non-zero if ready to send
 */
int uart_tx_ready(void);

/**
 * @brief Check if receive FIFO has data
 * @return Non-zero if data available
 */
int uart_rx_ready(void);

/**
 * @brief Send a single character (blocking)
 */
void uart_putchar(char c);

/**
 * @brief Receive a single character (blocking)
 */
char uart_getchar(void);

/**
 * @brief Send a null-terminated string
 */
void uart_puts(const char *s);

/**
 * @brief Send a buffer of bytes
 */
void uart_write(const char *buf, uint32_t len);

/**
 * @brief Printf-like formatted output
 *
 * Supported format specifiers:
 * - %d, %i: signed decimal
 * - %u: unsigned decimal
 * - %x, %X: hexadecimal
 * - %p: pointer
 * - %s: string
 * - %c: character
 * - %%: literal percent
 *
 * Supports width and '0' padding, e.g., %08x
 * Supports 'l' and 'll' length modifiers
 */
void uart_printf(const char *fmt, ...);

#endif /* RPI3_UART_H */
