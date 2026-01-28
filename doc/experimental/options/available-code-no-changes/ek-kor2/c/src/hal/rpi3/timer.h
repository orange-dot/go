/**
 * @file timer.h
 * @brief ARM Generic Timer Header for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_TIMER_H
#define RPI3_TIMER_H

#include <stdint.h>

/**
 * @brief Initialize the ARM Generic Timer
 */
void timer_init(void);

/**
 * @brief Get current time in microseconds
 * @return Time since timer_init() in microseconds
 */
uint64_t timer_get_us(void);

/**
 * @brief Get current time in milliseconds
 * @return Time since timer_init() in milliseconds
 */
uint32_t timer_get_ms(void);

/**
 * @brief Busy-wait delay in microseconds
 * @param us Microseconds to wait
 */
void timer_delay_us(uint32_t us);

/**
 * @brief Busy-wait delay in milliseconds
 * @param ms Milliseconds to wait
 */
void timer_delay_ms(uint32_t ms);

/**
 * @brief Get timer frequency in Hz
 * @return Timer frequency (typically 19.2 MHz on Pi 3)
 */
uint64_t timer_get_frequency(void);

#endif /* RPI3_TIMER_H */
