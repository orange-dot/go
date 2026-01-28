/**
 * @file ekk_hal_x86.c
 * @brief EK-KOR HAL Implementation for x86_64 Bare Metal
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements EK-KOR HAL for bare-metal x86_64:
 * - TSC for microsecond timestamps
 * - Shared memory message queues (inter-core IPC)
 * - x86 atomics (CMPXCHG, LOCK prefix)
 * - CLI/STI for critical sections
 *
 * NOTE: This HAL is designed for bare-metal use (EK-OS, custom bootloaders).
 * Debug output (ekk_hal_printf) is implemented as a weak symbol that can be
 * overridden by the application to provide serial/VGA output.
 */

#include "ekk/ekk_hal.h"
#include "ekk/ekk_field.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* Field region at fixed physical address */
#ifndef EKK_X86_FIELD_REGION_ADDR
#define EKK_X86_FIELD_REGION_ADDR   0x300000UL
#endif

/* Message queues for inter-core IPC */
#ifndef EKK_X86_MSG_QUEUE_ADDR
#define EKK_X86_MSG_QUEUE_ADDR      0x310000UL
#endif

#define MSG_QUEUE_SIZE          64
#define MSG_MAX_LEN             64
#define MAX_CORES               16

/* ============================================================================
 * INTERNAL STRING FUNCTIONS (minimal, no libc dependency)
 * ============================================================================ */

static void *hal_memset(void *s, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

static void *hal_memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/* ============================================================================
 * TSC CALIBRATION
 * ============================================================================ */

static uint64_t g_tsc_freq_mhz = 1000;  /* Default 1 GHz, calibrated at init */
static uint64_t g_tsc_base = 0;

/**
 * @brief Read TSC (Time Stamp Counter)
 */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/**
 * @brief Read TSC with serialization (more accurate)
 */
static inline uint64_t rdtscp(void) {
    uint32_t lo, hi, aux;
    __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return ((uint64_t)hi << 32) | lo;
}

/**
 * @brief Calibrate TSC using PIT (Programmable Interval Timer)
 *
 * Uses PIT channel 2 to measure TSC frequency.
 * PIT runs at 1.193182 MHz.
 */
static void calibrate_tsc(void) {
    /* PIT ports */
    #define PIT_CH2_DATA    0x42
    #define PIT_CMD         0x43
    #define PIT_CH2_GATE    0x61

    /* Set up PIT channel 2 for one-shot mode */
    /* Mode 0, binary, LSB then MSB */
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xB0), "Nd"((uint16_t)PIT_CMD));

    /* Load count (65535 for ~54.9ms delay at 1.193182 MHz) */
    uint16_t pit_count = 65535;
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(pit_count & 0xFF)), "Nd"((uint16_t)PIT_CH2_DATA));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(pit_count >> 8)), "Nd"((uint16_t)PIT_CH2_DATA));

    /* Enable PIT channel 2 gate */
    uint8_t gate;
    __asm__ volatile("inb %1, %0" : "=a"(gate) : "Nd"((uint16_t)PIT_CH2_GATE));
    gate = (gate & 0xFC) | 0x01;  /* Enable gate, disable speaker */
    __asm__ volatile("outb %0, %1" :: "a"(gate), "Nd"((uint16_t)PIT_CH2_GATE));

    /* Simple calibration: assume ~3 GHz (QEMU default) */
    /* For accurate calibration, we'd need ACPI PM timer or HPET */
    g_tsc_freq_mhz = 3000;

    /* Read TSC base */
    g_tsc_base = rdtsc();
}

/* ============================================================================
 * TIME FUNCTIONS
 * ============================================================================ */

ekk_time_us_t ekk_hal_time_us(void) {
    uint64_t tsc = rdtsc() - g_tsc_base;
    return tsc / g_tsc_freq_mhz;
}

void ekk_hal_delay_us(uint32_t us) {
    uint64_t start = rdtsc();
    uint64_t cycles = (uint64_t)us * g_tsc_freq_mhz;
    while ((rdtsc() - start) < cycles) {
        __asm__ volatile("pause");
    }
}

/* Mock time support for testing (stub - not used on bare metal) */
void ekk_hal_set_mock_time(ekk_time_us_t time_us) {
    (void)time_us;
    /* Not supported on bare metal x86 */
}

/* ============================================================================
 * MESSAGE QUEUE STRUCTURES
 * ============================================================================ */

typedef struct {
    ekk_module_id_t sender;
    uint8_t msg_type;
    uint8_t data[MSG_MAX_LEN];
    uint32_t len;
    volatile uint32_t valid;
} hal_message_t;

typedef struct {
    hal_message_t slots[MSG_QUEUE_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    uint8_t _pad[64 - 2 * sizeof(uint32_t)];  /* Cache line padding */
} core_queue_t;

/* Message queues at fixed address */
static core_queue_t *g_queues = (core_queue_t *)EKK_X86_MSG_QUEUE_ADDR;

/* Field region at fixed address */
static ekk_field_region_t *g_field_region = (ekk_field_region_t *)EKK_X86_FIELD_REGION_ADDR;

/* This core's module ID (set during init, based on APIC ID) */
static ekk_module_id_t g_module_id = 1;

/* Receive callback */
static ekk_hal_recv_cb g_recv_callback = NULL;

/* ============================================================================
 * CRITICAL SECTIONS
 * ============================================================================ */

uint32_t ekk_hal_critical_enter(void) {
    uint64_t flags;
    __asm__ volatile(
        "pushfq\n\t"
        "cli\n\t"
        "popq %0"
        : "=r"(flags)
        :
        : "memory"
    );
    return (uint32_t)(flags & 0x200);  /* Return IF flag state */
}

void ekk_hal_critical_exit(uint32_t state) {
    if (state) {
        __asm__ volatile("sti" ::: "memory");
    }
}

void ekk_hal_memory_barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

/* ============================================================================
 * ATOMIC OPERATIONS
 * ============================================================================ */

bool ekk_hal_cas32(volatile uint32_t *ptr, uint32_t expected, uint32_t desired) {
    uint8_t result;
    __asm__ volatile(
        "lock cmpxchgl %3, %1\n\t"
        "sete %0"
        : "=q"(result), "+m"(*ptr), "+a"(expected)
        : "r"(desired)
        : "cc", "memory"
    );
    return result;
}

uint32_t ekk_hal_atomic_inc(volatile uint32_t *ptr) {
    return __sync_add_and_fetch(ptr, 1);
}

uint32_t ekk_hal_atomic_dec(volatile uint32_t *ptr) {
    return __sync_sub_and_fetch(ptr, 1);
}

/* ============================================================================
 * MESSAGE TRANSMISSION
 * ============================================================================ */

ekk_error_t ekk_hal_send(ekk_module_id_t dest_id,
                          ekk_msg_type_t msg_type,
                          const void *data,
                          uint32_t len) {
    if (len > MSG_MAX_LEN) {
        return EKK_ERR_INVALID_ARG;
    }

    /* For broadcast, send to all queues */
    if (dest_id == EKK_BROADCAST_ID) {
        for (int i = 0; i < MAX_CORES; i++) {
            if ((ekk_module_id_t)(i + 1) != g_module_id) {
                ekk_hal_send(i + 1, msg_type, data, len);
            }
        }
        return EKK_OK;
    }

    /* Validate destination */
    if (dest_id == 0 || dest_id > MAX_CORES) {
        return EKK_ERR_INVALID_ARG;
    }

    core_queue_t *q = &g_queues[dest_id - 1];
    uint32_t head = q->head;
    uint32_t next_head = (head + 1) % MSG_QUEUE_SIZE;

    /* Check if full */
    if (next_head == q->tail) {
        return EKK_ERR_NO_MEMORY;
    }

    hal_message_t *slot = &q->slots[head];
    slot->sender = g_module_id;
    slot->msg_type = msg_type;
    slot->len = len;
    if (data && len > 0) {
        hal_memcpy(slot->data, data, len);
    }

    ekk_hal_memory_barrier();
    slot->valid = 1;
    ekk_hal_memory_barrier();

    q->head = next_head;
    return EKK_OK;
}

ekk_error_t ekk_hal_broadcast(ekk_msg_type_t msg_type,
                               const void *data,
                               uint32_t len) {
    return ekk_hal_send(EKK_BROADCAST_ID, msg_type, data, len);
}

ekk_error_t ekk_hal_recv(ekk_module_id_t *sender_id,
                          ekk_msg_type_t *msg_type,
                          void *data,
                          uint32_t *len) {
    core_queue_t *q = &g_queues[g_module_id - 1];

    if (q->tail == q->head) {
        return EKK_ERR_NOT_FOUND;
    }

    hal_message_t *slot = &q->slots[q->tail];
    ekk_hal_memory_barrier();

    if (!slot->valid) {
        return EKK_ERR_NOT_FOUND;
    }

    *sender_id = slot->sender;
    *msg_type = (ekk_msg_type_t)slot->msg_type;
    if (data && slot->len > 0) {
        uint32_t copy_len = (*len < slot->len) ? *len : slot->len;
        hal_memcpy(data, slot->data, copy_len);
    }
    *len = slot->len;

    slot->valid = 0;
    ekk_hal_memory_barrier();

    q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
    return EKK_OK;
}

void ekk_hal_set_recv_callback(ekk_hal_recv_cb callback) {
    g_recv_callback = callback;
}

/* ============================================================================
 * SHARED MEMORY
 * ============================================================================ */

void *ekk_hal_get_field_region(void) {
    return g_field_region;
}

void ekk_hal_sync_field_region(void) {
    ekk_hal_memory_barrier();
}

/* ============================================================================
 * LOCAL APIC ID
 * ============================================================================ */

/**
 * @brief Read Local APIC ID to determine core number
 */
static uint8_t get_apic_id(void) {
    /* Local APIC ID register is at 0xFEE00020 */
    volatile uint32_t *lapic_id = (volatile uint32_t *)0xFEE00020UL;
    return (uint8_t)((*lapic_id >> 24) & 0xFF);
}

/* ============================================================================
 * PLATFORM INITIALIZATION
 * ============================================================================ */

ekk_error_t ekk_hal_init(void) {
    /* Calibrate TSC */
    calibrate_tsc();

    /* Get module ID from APIC ID */
    g_module_id = get_apic_id() + 1;
    if (g_module_id == 0) g_module_id = 1;
    if (g_module_id > MAX_CORES) g_module_id = MAX_CORES;

    /* Initialize field region */
    hal_memset(g_field_region, 0, sizeof(ekk_field_region_t));

    /* Initialize message queues (only BSP does this) */
    if (g_module_id == 1) {
        hal_memset(g_queues, 0, sizeof(core_queue_t) * MAX_CORES);
    }

    ekk_hal_printf("HAL initialized for x86_64\n");
    ekk_hal_printf("Module ID: %u (APIC ID: %u)\n", g_module_id, g_module_id - 1);
    ekk_hal_printf("TSC frequency: ~%lu MHz\n", (unsigned long)g_tsc_freq_mhz);

    return EKK_OK;
}

const char *ekk_hal_platform_name(void) {
    return "x86_64 (EK-KOR)";
}

ekk_module_id_t ekk_hal_get_module_id(void) {
    return g_module_id;
}

/* ============================================================================
 * DEBUG OUTPUT (WEAK - override in application)
 * ============================================================================ */

/**
 * @brief Debug print function (weak symbol)
 *
 * Override this in your application to provide serial/VGA output.
 * Default implementation does nothing.
 */
__attribute__((weak))
void ekk_hal_printf(const char *fmt, ...) {
    (void)fmt;
    /* Default: no output. Override in application. */
}

/**
 * @brief Assert failure handler (weak symbol)
 *
 * Override this in your application for custom handling.
 */
__attribute__((weak))
void ekk_hal_assert_fail(const char *file, int line, const char *expr) {
    ekk_hal_printf("\n!!! EK-KOR ASSERT FAILED !!!\n");
    ekk_hal_printf("File: %s\n", file);
    ekk_hal_printf("Line: %d\n", line);
    ekk_hal_printf("Expr: %s\n", expr);

    /* Halt */
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}
