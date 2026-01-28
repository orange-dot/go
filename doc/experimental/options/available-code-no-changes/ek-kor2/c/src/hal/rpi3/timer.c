/**
 * @file timer.c
 * @brief ARM Generic Timer Driver for Raspberry Pi 3B+ (BCM2837B0)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Uses the ARM Generic Timer (CNTPCT_EL0) which runs at 19.2 MHz on Pi 3.
 * Provides microsecond-precision timestamps.
 */

#include "rpi3_hw.h"
#include "timer.h"

/* ============================================================================
 * Private Variables
 * ============================================================================ */

/* Base timestamp (set at init) */
static uint64_t g_timer_base = 0;

/* Timer frequency in Hz (read from CNTFRQ_EL0) */
static uint64_t g_timer_freq_hz = ARM_TIMER_FREQ_HZ;

/* ============================================================================
 * Timer Functions
 * ============================================================================ */

/**
 * @brief Initialize the ARM Generic Timer
 */
void timer_init(void)
{
    uint64_t freq;

    /* Read timer frequency from system register */
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));

    /* Store frequency (should be 19200000 on Pi 3) */
    if (freq > 0) {
        g_timer_freq_hz = freq;
    }

    /* Enable timer access at EL0 (in case we drop to EL0 later) */
    uint64_t cntkctl;
    __asm__ volatile("mrs %0, cntkctl_el1" : "=r"(cntkctl));
    cntkctl |= (1 << 0);            /* EL0PCTEN: Enable physical counter at EL0 */
    cntkctl |= (1 << 1);            /* EL0VCTEN: Enable virtual counter at EL0 */
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(cntkctl));

    /* Read base time */
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(g_timer_base));
}

/**
 * @brief Get current timer count (raw)
 */
static inline uint64_t timer_read_count(void)
{
    uint64_t count;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(count));
    return count;
}

/**
 * @brief Get current time in microseconds
 *
 * Uses the physical counter which runs at 19.2 MHz.
 * Resolution: ~52 ns (1/19.2 MHz)
 *
 * @return Time in microseconds since timer_init()
 */
uint64_t timer_get_us(void)
{
    uint64_t count = timer_read_count() - g_timer_base;

    /* Convert to microseconds: count / (freq / 1000000) = count * 1000000 / freq */
    /* For 19.2 MHz: count / 19.2 = count * 1000000 / 19200000 */
    /* Simplified: count / 19 (with small error) or exact division */

    /* Exact calculation (avoids overflow for reasonable counts) */
    return (count * 1000000ULL) / g_timer_freq_hz;
}

/**
 * @brief Get current time in milliseconds
 */
uint32_t timer_get_ms(void)
{
    return (uint32_t)(timer_get_us() / 1000);
}

/**
 * @brief Busy-wait delay in microseconds
 *
 * @param us Microseconds to wait
 */
void timer_delay_us(uint32_t us)
{
    uint64_t start = timer_read_count();
    uint64_t ticks = ((uint64_t)us * g_timer_freq_hz) / 1000000ULL;
    uint64_t target = start + ticks;

    while (timer_read_count() < target) {
        __asm__ volatile("yield");  /* Hint to reduce power consumption */
    }
}

/**
 * @brief Busy-wait delay in milliseconds
 *
 * @param ms Milliseconds to wait
 */
void timer_delay_ms(uint32_t ms)
{
    timer_delay_us(ms * 1000);
}

/**
 * @brief Get timer frequency in Hz
 */
uint64_t timer_get_frequency(void)
{
    return g_timer_freq_hz;
}
