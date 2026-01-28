/**
 * @file ekk_hal_rpi3.c
 * @brief EK-KOR v2 HAL Implementation for Raspberry Pi 3B+ (BCM2837B0)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * This HAL implements the EK-KOR hardware abstraction layer for the
 * Raspberry Pi 3B+ running in bare-metal AArch64 mode with SMP support.
 *
 * Each of the 4 CPU cores acts as an EK-KOR module, with module IDs 1-4.
 * Inter-module communication uses shared memory message queues.
 */

#include "ekk/ekk_hal.h"
#include "ekk/ekk_types.h"

#include "rpi3_hw.h"
#include "uart.h"
#include "timer.h"
#include "gic.h"
#include "smp.h"
#include "msg_queue.h"
#include "framebuffer.h"
#include "mailbox.h"
#include "fs_server.h"
#include "ekkfs_test.h"
#include "ekkdb_test.h"
#include "dashboard.h"
#include "shell.h"
#include "usb_dwc2.h"

#include <stdarg.h>
#include <string.h>

/* Enable EKKFS tests at boot (set to 1 to run tests) */
#ifndef EKKFS_RUN_TESTS
#define EKKFS_RUN_TESTS         0
#endif

/* ============================================================================
 * External Symbols
 * ============================================================================ */

/* Field region from linker script */
extern char __field_region[];

/* Exception vectors from boot.S */
extern char exception_vectors[];

/* ============================================================================
 * Private Variables
 * ============================================================================ */

/* Mock time for testing (0 = use real time) */
static uint64_t g_mock_time = 0;
static int g_mock_time_enabled = 0;

/* Receive callback */
static ekk_hal_recv_cb g_recv_callback = NULL;

/* Initialization flag */
static volatile int g_hal_initialized = 0;

/* ============================================================================
 * TIME FUNCTIONS
 * ============================================================================ */

/**
 * @brief Get current time in microseconds
 */
ekk_time_us_t ekk_hal_time_us(void)
{
    if (g_mock_time_enabled) {
        return g_mock_time;
    }
    return timer_get_us();
}

/**
 * @brief Busy-wait delay in microseconds
 */
void ekk_hal_delay_us(uint32_t us)
{
    if (g_mock_time_enabled) {
        g_mock_time += us;
        return;
    }
    timer_delay_us(us);
}

/**
 * @brief Set mock time for testing
 */
void ekk_hal_set_mock_time(ekk_time_us_t time_us)
{
    if (time_us == 0) {
        g_mock_time_enabled = 0;
        g_mock_time = 0;
    } else {
        g_mock_time_enabled = 1;
        g_mock_time = time_us;
    }
}

/* ============================================================================
 * MESSAGE TRANSMISSION
 * ============================================================================ */

/**
 * @brief Send message to specific module
 *
 * On RPi3, module IDs 1-4 map to cores 0-3.
 */
ekk_error_t ekk_hal_send(ekk_module_id_t dest_id,
                          ekk_msg_type_t msg_type,
                          const void *data,
                          uint32_t len)
{
    /* Validate */
    if (len > 64) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Get sender ID (this core's module ID) */
    uint8_t sender_id = ekk_hal_get_module_id();

    /* Handle broadcast */
    if (dest_id == EKK_BROADCAST_ID) {
        if (msg_queue_send(MSG_BROADCAST, sender_id, msg_type, data, len) < 0) {
            return EKK_ERR_NO_MEMORY;
        }
        return EKK_OK;
    }

    /* Validate destination */
    if (dest_id < 1 || dest_id > 4) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Map module ID to core (module 1 = core 0, etc.) */
    uint32_t dest_core = dest_id - 1;

    if (msg_queue_send(dest_core, sender_id, msg_type, data, len) < 0) {
        return EKK_ERR_NO_MEMORY;
    }

    return EKK_OK;
}

/**
 * @brief Broadcast message to all modules
 */
ekk_error_t ekk_hal_broadcast(ekk_msg_type_t msg_type,
                               const void *data,
                               uint32_t len)
{
    return ekk_hal_send(EKK_BROADCAST_ID, msg_type, data, len);
}

/**
 * @brief Check for received message
 */
ekk_error_t ekk_hal_recv(ekk_module_id_t *sender_id,
                          ekk_msg_type_t *msg_type,
                          void *data,
                          uint32_t *len)
{
    uint8_t s_id, m_type;

    if (msg_queue_recv(&s_id, &m_type, data, len) < 0) {
        return EKK_ERR_NOT_FOUND;
    }

    if (sender_id) *sender_id = s_id;
    if (msg_type) *msg_type = (ekk_msg_type_t)m_type;

    return EKK_OK;
}

/**
 * @brief Register receive callback
 */
void ekk_hal_set_recv_callback(ekk_hal_recv_cb callback)
{
    g_recv_callback = callback;

    /* Also set message queue callback to forward */
    if (callback) {
        msg_queue_set_callback((msg_recv_callback_t)callback);
    } else {
        msg_queue_set_callback(NULL);
    }
}

/* ============================================================================
 * CRITICAL SECTIONS
 * ============================================================================ */

/**
 * @brief Enter critical section (disable interrupts)
 */
uint32_t ekk_hal_critical_enter(void)
{
    uint64_t daif;

    /* Read current DAIF and disable IRQ */
    __asm__ volatile(
        "mrs    %0, daif\n"
        "msr    daifset, #2"        /* Set I bit (disable IRQ) */
        : "=r"(daif)
        :
        : "memory"
    );

    return (uint32_t)(daif & 0x80); /* Return I bit state */
}

/**
 * @brief Exit critical section
 */
void ekk_hal_critical_exit(uint32_t state)
{
    if (state == 0) {
        /* IRQ was enabled, re-enable it */
        __asm__ volatile(
            "msr    daifclr, #2"    /* Clear I bit (enable IRQ) */
            :
            :
            : "memory"
        );
    }
}

/**
 * @brief Memory barrier
 */
void ekk_hal_memory_barrier(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

/* ============================================================================
 * ATOMIC OPERATIONS
 * ============================================================================ */

/**
 * @brief Atomic compare-and-swap
 */
bool ekk_hal_cas32(volatile uint32_t *ptr, uint32_t expected, uint32_t desired)
{
    uint32_t tmp, result;

    __asm__ volatile(
        "1: ldaxr   %w0, [%2]\n"        /* Load exclusive acquire */
        "   cmp     %w0, %w3\n"         /* Compare with expected */
        "   b.ne    2f\n"               /* Branch if not equal */
        "   stlxr   %w1, %w4, [%2]\n"   /* Store exclusive release */
        "   cbnz    %w1, 1b\n"          /* Retry if failed */
        "   mov     %w1, #1\n"          /* Success */
        "   b       3f\n"
        "2: mov     %w1, #0\n"          /* Failed */
        "3:\n"
        : "=&r"(tmp), "=&r"(result)
        : "r"(ptr), "r"(expected), "r"(desired)
        : "memory", "cc"
    );

    return result != 0;
}

/**
 * @brief Atomic increment
 */
uint32_t ekk_hal_atomic_inc(volatile uint32_t *ptr)
{
    uint32_t tmp, result;

    __asm__ volatile(
        "1: ldaxr   %w0, [%2]\n"        /* Load exclusive acquire */
        "   add     %w0, %w0, #1\n"     /* Increment */
        "   stlxr   %w1, %w0, [%2]\n"   /* Store exclusive release */
        "   cbnz    %w1, 1b\n"          /* Retry if failed */
        : "=&r"(result), "=&r"(tmp)
        : "r"(ptr)
        : "memory"
    );

    return result;
}

/**
 * @brief Atomic decrement
 */
uint32_t ekk_hal_atomic_dec(volatile uint32_t *ptr)
{
    uint32_t tmp, result;

    __asm__ volatile(
        "1: ldaxr   %w0, [%2]\n"        /* Load exclusive acquire */
        "   sub     %w0, %w0, #1\n"     /* Decrement */
        "   stlxr   %w1, %w0, [%2]\n"   /* Store exclusive release */
        "   cbnz    %w1, 1b\n"          /* Retry if failed */
        : "=&r"(result), "=&r"(tmp)
        : "r"(ptr)
        : "memory"
    );

    return result;
}

/* ============================================================================
 * SHARED MEMORY
 * ============================================================================ */

/**
 * @brief Get pointer to shared field region
 */
void* ekk_hal_get_field_region(void)
{
    return (void *)__field_region;
}

/**
 * @brief Synchronize shared field region
 */
void ekk_hal_sync_field_region(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

/* ============================================================================
 * PLATFORM INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize HAL (called from each core)
 */
ekk_error_t ekk_hal_init(void)
{
    uint32_t core_id = smp_get_core_id();

    /* Core 0 does primary initialization */
    if (core_id == 0) {
        /* Initialize UART for debug output (fallback) */
        uart_init();
        uart_puts("UART initialized...\n");

        /* Initialize HDMI framebuffer (720p) */
        uart_puts("Initializing framebuffer...\n");
        if (framebuffer_init(1280, 720) == 0) {
            uart_puts("Framebuffer OK!\n");
            fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
            fb_puts("\n");
            fb_puts("===========================================\n");
            fb_puts("  EK-KOR v2 HAL for Raspberry Pi 3B+\n");
            fb_puts("  BCM2837B0 (Cortex-A53 x 4 @ 1.4 GHz)\n");
            fb_puts("===========================================\n");
            fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
            fb_puts("HDMI framebuffer: 1280x720 @ 32bpp\n");
        } else {
            uart_puts("WARNING: Framebuffer init failed, using UART only\n");
        }

        /* Also print to UART as fallback/serial console */
        uart_puts("\n");
        uart_puts("===========================================\n");
        uart_puts("  EK-KOR v2 HAL for Raspberry Pi 3B+\n");
        uart_puts("  BCM2837B0 (Cortex-A53 x 4 @ 1.4 GHz)\n");
        uart_puts("===========================================\n");

        /* Initialize timer */
        timer_init();
        uart_printf("Timer: %llu Hz\n", timer_get_frequency());
        if (framebuffer_is_ready()) {
            fb_printf("Timer: %llu Hz\n", timer_get_frequency());
        }

        /* Initialize GIC (with QEMU detection) */
        uart_puts("Initializing GIC...\n");
        gic_init();
        uart_puts("GIC initialized\n");

        /* Initialize SMP */
        smp_init();

        /* Initialize message queues */
        msg_queue_init();
        uart_puts("Message queues initialized\n");
        if (framebuffer_is_ready()) {
            fb_puts("Message queues initialized\n");
        }

        /* Install exception vectors */
        __asm__ volatile(
            "msr    vbar_el1, %0"
            :
            : "r"(exception_vectors)
        );

        /* Clear field region */
        memset(__field_region, 0, 64 * 1024);

        uart_puts("Core 0 initialization complete\n");
        if (framebuffer_is_ready()) {
            fb_puts("Core 0 initialization complete\n");
        }

        g_hal_initialized = 1;
        __asm__ volatile("dmb sy" ::: "memory");
    } else {
        /* Secondary cores wait for primary init */
        while (!g_hal_initialized) {
            __asm__ volatile("yield");
        }
        __asm__ volatile("dmb sy" ::: "memory");
    }

    /* Each core initializes its GIC CPU interface */
    gic_cpu_init();

    /* Each core initializes its message queue view */
    msg_queue_init();

    uart_printf("Core %d: HAL initialized (Module ID %d)\n",
                core_id, ekk_hal_get_module_id());
    if (framebuffer_is_ready()) {
        fb_printf("Core %d: HAL initialized (Module ID %d)\n",
                  core_id, ekk_hal_get_module_id());
    }

    return EKK_OK;
}

/**
 * @brief Get platform name
 */
const char* ekk_hal_platform_name(void)
{
    return "RPi3 BCM2837B0 (Cortex-A53 x 4)";
}

/**
 * @brief Get this module's hardware ID
 *
 * On RPi3, module ID = core ID + 1 (1-4).
 */
ekk_module_id_t ekk_hal_get_module_id(void)
{
    return (ekk_module_id_t)(smp_get_core_id() + 1);
}

/* ============================================================================
 * DEBUG OUTPUT
 * ============================================================================ */

/**
 * @brief Debug print (printf-like)
 *
 * Outputs to both HDMI framebuffer and UART serial console.
 */
void ekk_hal_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    /* Build output string for both framebuffer and UART */
    uart_puts("[EKK] ");
    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_CYAN, FB_COLOR_BLACK);
        fb_puts("[EKK] ");
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }

    /* Parse format string manually */
    const char *p = fmt;
    while (*p) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 'd':
                case 'i': {
                    int val = va_arg(args, int);
                    uart_printf("%d", val);
                    if (framebuffer_is_ready()) fb_printf("%d", val);
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    uart_printf("%u", val);
                    if (framebuffer_is_ready()) fb_printf("%u", val);
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned int val = va_arg(args, unsigned int);
                    uart_printf("%x", val);
                    if (framebuffer_is_ready()) fb_printf("%x", val);
                    break;
                }
                case 's': {
                    const char *str = va_arg(args, const char *);
                    uart_puts(str);
                    if (framebuffer_is_ready()) fb_puts(str);
                    break;
                }
                case 'c': {
                    char ch = (char)va_arg(args, int);
                    uart_putchar(ch);
                    if (framebuffer_is_ready()) fb_putchar(ch);
                    break;
                }
                case '%':
                    uart_putchar('%');
                    if (framebuffer_is_ready()) fb_putchar('%');
                    break;
                default:
                    uart_putchar('%');
                    uart_putchar(*p);
                    if (framebuffer_is_ready()) {
                        fb_putchar('%');
                        fb_putchar(*p);
                    }
                    break;
            }
        } else {
            if (*p == '\n') uart_putchar('\r');
            uart_putchar(*p);
            if (framebuffer_is_ready()) fb_putchar(*p);
        }
        p++;
    }

    va_end(args);
}

/**
 * @brief Assert handler
 */
void ekk_hal_assert_fail(const char *file, int line, const char *expr)
{
    /* Disable interrupts */
    __asm__ volatile("msr daifset, #3" ::: "memory");

    uart_puts("\n!!! EK-KOR ASSERTION FAILED !!!\n");
    uart_printf("File: %s\n", file);
    uart_printf("Line: %d\n", line);
    uart_printf("Expr: %s\n", expr);
    uart_printf("Core: %d\n", smp_get_core_id());

    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_RED, FB_COLOR_BLACK);
        fb_puts("\n!!! EK-KOR ASSERTION FAILED !!!\n");
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
        fb_printf("File: %s\n", file);
        fb_printf("Line: %d\n", line);
        fb_printf("Expr: %s\n", expr);
        fb_printf("Core: %d\n", smp_get_core_id());
    }

    /* Halt */
    while (1) {
        __asm__ volatile("wfe");
    }
}

/* ============================================================================
 * KERNEL ENTRY POINT
 * ============================================================================ */

/**
 * @brief Secondary core entry point
 *
 * Called by boot.S after secondary core wakes up.
 * Core 3 runs the filesystem server.
 */
static void secondary_main(uint32_t core_id)
{
    /* Initialize HAL for this core */
    ekk_hal_init();

    uart_printf("Core %d: Ready\n", core_id);
    if (framebuffer_is_ready()) {
        fb_printf("Core %d: Ready\n", core_id);
    }

    /* Core 3 runs the filesystem server */
    if (core_id == 3) {
        uart_puts("Core 3: Starting FS server...\n");
        if (framebuffer_is_ready()) {
            fb_puts("Core 3: Starting FS server...\n");
        }

        if (fs_server_init() == 0) {
            uart_puts("Core 3: FS server initialized\n");
            if (framebuffer_is_ready()) {
                fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
                fb_puts("FS Server: Ready\n");
                fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
            }

#if EKKFS_RUN_TESTS
            /* Run EKKDB tests before entering main loop */
            uart_puts("\nCore 3: Running EKKDB tests...\n");
            if (framebuffer_is_ready()) {
                fb_puts("\nRunning EKKDB tests...\n");
            }
            ekkdb_test_run();
            uart_puts("\nCore 3: EKKDB tests complete, entering main loop\n");
            if (framebuffer_is_ready()) {
                fb_puts("EKKDB tests complete\n");
            }
#endif

            /* Run FS server main loop (does not return) */
            fs_server_main();
        } else {
            uart_puts("Core 3: FS server init failed!\n");
            if (framebuffer_is_ready()) {
                fb_set_colors(FB_COLOR_RED, FB_COLOR_BLACK);
                fb_puts("FS Server: Init failed!\n");
                fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
            }
        }
    }

    /* Simple idle loop for other cores */
    while (1) {
        __asm__ volatile("wfe");
    }
}

/**
 * @brief Main kernel entry point (called from boot.S)
 */
void kernel_main(void)
{
    /* Primary initialization */
    ekk_hal_init();

#if EKKFS_RUN_TESTS
    /* Run EKKFS tests on Core 0 before starting other cores */
    uart_puts("\n");
    if (framebuffer_is_ready()) {
        fb_puts("\n");
    }

    int test_failures = ekkfs_run_all_tests();

    if (test_failures == 0) {
        uart_puts("\nEKKFS tests completed successfully.\n");
        uart_puts("Continuing with normal boot...\n\n");
        if (framebuffer_is_ready()) {
            fb_puts("\nContinuing with normal boot...\n\n");
        }
    } else {
        uart_printf("\nWARNING: %d EKKFS tests failed!\n", test_failures);
        uart_puts("Continuing anyway...\n\n");
        if (framebuffer_is_ready()) {
            fb_set_colors(FB_COLOR_YELLOW, FB_COLOR_BLACK);
            fb_printf("\nWARNING: %d tests failed!\n", test_failures);
            fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
        }
    }

    /* Small delay before continuing */
    timer_delay_us(500000);
#endif

    /* Start secondary cores */
    uart_puts("\nStarting secondary cores...\n");
    if (framebuffer_is_ready()) {
        fb_puts("\nStarting secondary cores...\n");
    }
    smp_start_all_cores(secondary_main);

    /* Wait for all cores to be ready */
    uart_puts("Waiting for all cores...\n");
    uint32_t ready = smp_wait_all_cores_ready(1000000);
    uart_printf("%d cores ready\n", ready);
    if (framebuffer_is_ready()) {
        fb_printf("%d cores ready\n", ready);
    }

    /* Done */
    uart_puts("\n=== ALL CORES BOOTED ===\n");
    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
        fb_puts("\n=== ALL CORES BOOTED ===\n");
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }

    /* Display hardware info from VideoCore mailbox */
    uart_puts("\n--- Hardware Info ---\n");
    if (framebuffer_is_ready()) {
        fb_puts("\n--- Hardware Info ---\n");
    }

    uart_puts("Reading SoC temperature...\n");
    int32_t temp = mbox_get_temperature();
    uart_puts("Reading max temperature...\n");
    int32_t max_temp = mbox_get_max_temperature();
    uart_puts("Reading ARM clock...\n");
    uint32_t arm_clock = mbox_get_arm_clock();
    uart_puts("Done reading hardware info.\n");
    if (temp > 0) {
        uart_printf("SoC Temperature: %d.%d C\n", temp / 1000, (temp % 1000) / 100);
    } else {
        uart_puts("SoC Temperature: N/A\n");
    }
    if (max_temp > 0) {
        uart_printf("Max Temperature: %d.%d C\n", max_temp / 1000, (max_temp % 1000) / 100);
    }
    if (arm_clock > 0) {
        uart_printf("ARM Clock: %d MHz\n", arm_clock / 1000000);
    } else {
        uart_puts("ARM Clock: N/A\n");
    }

    if (framebuffer_is_ready()) {
        if (temp > 0) {
            fb_printf("SoC Temperature: %d.%d C\n", temp / 1000, (temp % 1000) / 100);
        } else {
            fb_puts("SoC Temperature: N/A\n");
        }
        if (max_temp > 0) {
            fb_printf("Max Temperature: %d.%d C\n", max_temp / 1000, (max_temp % 1000) / 100);
        }
        if (arm_clock > 0) {
            fb_printf("ARM Clock: %d MHz\n", arm_clock / 1000000);
        } else {
            fb_puts("ARM Clock: N/A\n");
        }
    }

    /* Initialize USB host controller */
    uart_puts("\n--- USB Initialization ---\n");
    if (framebuffer_is_ready()) {
        fb_puts("\n--- USB Initialization ---\n");
    }

    if (usb_init() == 0) {
        if (usb_keyboard_ready()) {
            uart_puts("USB: Keyboard ready - type on USB keyboard!\n");
            if (framebuffer_is_ready()) {
                fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
                fb_puts("USB Keyboard: Ready\n");
                fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
            }
        } else if (usb_device_connected()) {
            uart_puts("USB: Device connected (not a keyboard)\n");
            if (framebuffer_is_ready()) {
                fb_puts("USB: Device connected (not keyboard)\n");
            }
        } else {
            uart_puts("USB: No device connected\n");
            if (framebuffer_is_ready()) {
                fb_puts("USB: No device connected\n");
            }
        }
    } else {
        uart_puts("USB: Initialization failed (keyboard via UART only)\n");
        if (framebuffer_is_ready()) {
            fb_set_colors(FB_COLOR_YELLOW, FB_COLOR_BLACK);
            fb_puts("USB: Init failed (UART only)\n");
            fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
        }
    }

    /* Wait a bit before starting shell */
    uart_puts("\nWaiting 1s before shell...\n");
    timer_delay_us(1000000);

    /* Initialize and run shell (never returns) */
    shell_init();
    shell_run();
}

/* ============================================================================
 * IRQ HANDLER
 * ============================================================================ */

/**
 * @brief C IRQ handler (called from boot.S)
 */
void irq_handler_c(void)
{
    uint32_t irq = gic_ack_irq();

    if (irq == 1023) {
        /* Spurious interrupt */
        return;
    }

    /* Handle interrupt based on IRQ number */
    /* For now, just acknowledge it */

    gic_end_irq(irq);
}
