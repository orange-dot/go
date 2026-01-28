/**
 * @file smp.c
 * @brief SMP (Symmetric Multi-Processing) Support for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Manages the 4 Cortex-A53 cores:
 * - Core 0: Primary core (runs kernel_main)
 * - Cores 1-3: Secondary cores (wait in spin_table)
 *
 * Secondary cores are started by writing their entry point to the
 * spin_table and issuing an SEV (Send Event) instruction.
 */

#include "rpi3_hw.h"
#include "smp.h"
#include "gic.h"
#include "uart.h"

/* ============================================================================
 * External Symbols
 * ============================================================================ */

/* Assembly entry point for secondary cores (boot.S) */
extern void secondary_entry_asm(void);

/* Stack boundaries from linker script */
extern char __stack_start[];

/* ============================================================================
 * GPU Firmware Mailbox Addresses (Real Hardware)
 * ============================================================================
 *
 * On real RPi3 with kernel8.img, the GPU firmware holds secondary cores
 * spinning at these mailbox addresses. Writing a non-zero address releases
 * the core to jump to that address (in EL2, no stack).
 *
 * Memory map:
 *   0x00 - 0xD7: ARM stub / reserved
 *   0xD8: Core 0 release (not used, Core 0 starts automatically)
 *   0xE0: Core 1 release address
 *   0xE8: Core 2 release address
 *   0xF0: Core 3 release address
 */
#define GPU_RELEASE_CORE1   ((volatile uint64_t *)0xE0)
#define GPU_RELEASE_CORE2   ((volatile uint64_t *)0xE8)
#define GPU_RELEASE_CORE3   ((volatile uint64_t *)0xF0)

/* ============================================================================
 * Private Variables
 * ============================================================================ */

/* Per-core initialization complete flags */
static volatile uint32_t g_core_ready[4] = {0, 0, 0, 0};

/* Secondary core entry function (set before starting cores) */
static void (*g_secondary_entry)(uint32_t core_id) = 0;

/* ============================================================================
 * Core Identification
 * ============================================================================ */

/**
 * @brief Get current core ID
 * @return Core ID (0-3)
 */
uint32_t smp_get_core_id(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFF);
}

/**
 * @brief Get number of cores
 * @return Number of cores (4 on Pi 3)
 */
uint32_t smp_get_num_cores(void)
{
    return 4;
}

/* ============================================================================
 * Secondary Core Entry Point
 * ============================================================================ */

/**
 * @brief C entry point for secondary cores (called from boot.S)
 *
 * Called by secondary_entry_asm after EL2→EL1 transition and stack setup.
 *
 * @param core_id The core ID (1-3) passed from assembly
 */
void secondary_core_c_entry(uint32_t core_id)
{
    /* Debug output */
    uart_printf("Core %d: entered C code\n", core_id);

    /* Initialize this core's GIC CPU interface */
    gic_cpu_init();

    /* Enable interrupts */
    __asm__ volatile("msr daifclr, #2");  /* Clear I bit */

    /* Mark core as ready */
    __asm__ volatile("dmb sy" ::: "memory");
    g_core_ready[core_id] = 1;
    __asm__ volatile("dmb sy" ::: "memory");

    uart_printf("Core %d: ready, calling user entry\n", core_id);

    /* Call user-provided entry function if set */
    if (g_secondary_entry) {
        g_secondary_entry(core_id);
    }

    /* Default: halt */
    uart_printf("Core %d: halting\n", core_id);
    while (1) {
        __asm__ volatile("wfe");
    }
}

/* ============================================================================
 * SMP Control Functions
 * ============================================================================ */

/**
 * @brief Initialize SMP (called by core 0)
 */
void smp_init(void)
{
    /* Mark core 0 as ready */
    g_core_ready[0] = 1;
}

/**
 * @brief Start a secondary core
 *
 * @param core_id Core to start (1-3)
 * @param entry Entry point function (receives core_id)
 * @return 0 on success, -1 on error
 *
 * On real RPi3 hardware, secondary cores are held by the GPU firmware
 * spinning on mailbox addresses 0xE0/0xE8/0xF0. Writing a non-zero
 * address releases the core to jump there (in EL2, no stack).
 *
 * We write the address of secondary_entry_asm which:
 *   1. Drops from EL2 to EL1
 *   2. Sets up per-core stack
 *   3. Calls secondary_core_c_entry()
 */
int smp_start_core(uint32_t core_id, void (*entry)(uint32_t))
{
    volatile uint64_t *release_addr;

    if (core_id < 1 || core_id > 3) {
        return -1;  /* Invalid core ID */
    }

    if (g_core_ready[core_id]) {
        uart_printf("Core %d: already running\n", core_id);
        return -1;  /* Core already running */
    }

    /* Store user entry function (called by secondary_core_c_entry) */
    g_secondary_entry = entry;

    /* Get assembly entry point address */
    uint64_t asm_entry = (uint64_t)(uintptr_t)secondary_entry_asm;

    /* Select GPU release address for this core */
    switch (core_id) {
        case 1: release_addr = GPU_RELEASE_CORE1; break;
        case 2: release_addr = GPU_RELEASE_CORE2; break;
        case 3: release_addr = GPU_RELEASE_CORE3; break;
        default: return -1;
    }

    uart_printf("Core %d: writing 0x%llx to release addr 0x%llx\n",
                core_id, asm_entry, (uint64_t)(uintptr_t)release_addr);

    /* Memory barrier before write */
    __asm__ volatile("dmb sy" ::: "memory");

    /* Write entry point to GPU mailbox - this releases the core! */
    *release_addr = asm_entry;

    /* Ensure write is visible to other cores */
    __asm__ volatile("dmb sy" ::: "memory");

    /* Clean data cache line containing the release address */
    __asm__ volatile("dc cvac, %0" :: "r"(release_addr) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");

    /* Send event to wake cores from WFE */
    __asm__ volatile("sev");

    uart_printf("Core %d: released\n", core_id);

    return 0;
}

/**
 * @brief Start all secondary cores
 *
 * @param entry Entry point function for all secondary cores
 */
void smp_start_all_cores(void (*entry)(uint32_t))
{
    uart_puts("smp_start_all_cores: starting...\n");
    for (uint32_t i = 1; i <= 3; i++) {
        uart_printf("Starting core %d...\n", i);
        smp_start_core(i, entry);

        /* Small delay between core starts to avoid race conditions */
        for (volatile int j = 0; j < 100000; j++) { }
    }
    uart_puts("smp_start_all_cores: done\n");
}

/**
 * @brief Wait for a core to be ready
 *
 * @param core_id Core to wait for (0-3)
 * @param timeout_us Timeout in microseconds (0 = infinite)
 * @return 0 if ready, -1 on timeout
 */
int smp_wait_core_ready(uint32_t core_id, uint32_t timeout_us)
{
    if (core_id > 3) {
        return -1;
    }

    /* Simple timeout with busy loop */
    uint32_t loops = 0;
    uint32_t max_loops = timeout_us / 10;  /* ~10us per loop estimate */
    if (max_loops == 0) max_loops = 100000;

    while (!g_core_ready[core_id]) {
        loops++;
        if (loops > max_loops) {
            uart_printf("Core %d: timeout waiting\n", core_id);
            return -1;
        }
        for (volatile int i = 0; i < 100; i++) { }  /* Small delay */
        __asm__ volatile("yield");
    }

    return 0;
}

/**
 * @brief Wait for all cores to be ready
 *
 * @param timeout_us Timeout in microseconds per core (0 = infinite)
 * @return Number of ready cores
 */
uint32_t smp_wait_all_cores_ready(uint32_t timeout_us)
{
    uint32_t ready = 0;

    for (uint32_t i = 0; i < 4; i++) {
        if (smp_wait_core_ready(i, timeout_us) == 0) {
            ready++;
        }
    }

    return ready;
}

/**
 * @brief Check if a core is ready
 *
 * @param core_id Core to check (0-3)
 * @return Non-zero if ready
 */
int smp_is_core_ready(uint32_t core_id)
{
    if (core_id > 3) {
        return 0;
    }
    return g_core_ready[core_id];
}

/* ============================================================================
 * Inter-Processor Interrupt (IPI) Support
 * ============================================================================ */

/**
 * @brief Send IPI to a specific core
 *
 * @param core_id Target core (0-3)
 * @param sgi_id SGI number (0-15)
 */
void smp_send_ipi(uint32_t core_id, uint8_t sgi_id)
{
    if (core_id > 3 || sgi_id > 15) {
        return;
    }
    gic_send_sgi(0, 1 << core_id, sgi_id);
}

/**
 * @brief Send IPI to all other cores
 *
 * @param sgi_id SGI number (0-15)
 */
void smp_send_ipi_all(uint8_t sgi_id)
{
    if (sgi_id > 15) {
        return;
    }
    gic_send_sgi(1, 0, sgi_id);  /* target_filter=1: all except self */
}

/* ============================================================================
 * Simple Spin Lock
 * ============================================================================ */

/**
 * @brief Initialize spin lock
 */
void smp_spinlock_init(smp_spinlock_t *lock)
{
    lock->locked = 0;
}

/**
 * @brief Acquire spin lock
 */
void smp_spinlock_lock(smp_spinlock_t *lock)
{
    uint32_t tmp;

    __asm__ volatile(
        "1: ldaxr   %w0, [%1]\n"        /* Load exclusive acquire */
        "   cbnz    %w0, 1b\n"          /* Spin if locked */
        "   stxr    %w0, %w2, [%1]\n"   /* Try to store 1 */
        "   cbnz    %w0, 1b\n"          /* Retry if failed */
        : "=&r"(tmp)
        : "r"(&lock->locked), "r"(1)
        : "memory"
    );
}

/**
 * @brief Try to acquire spin lock (non-blocking)
 * @return Non-zero if acquired
 */
int smp_spinlock_trylock(smp_spinlock_t *lock)
{
    uint32_t tmp, result;

    __asm__ volatile(
        "   ldaxr   %w0, [%2]\n"        /* Load exclusive acquire */
        "   cbnz    %w0, 1f\n"          /* Branch if locked */
        "   stxr    %w0, %w3, [%2]\n"   /* Try to store 1 */
        "   cbnz    %w0, 1f\n"          /* Branch if failed */
        "   mov     %w1, #1\n"          /* Success */
        "   b       2f\n"
        "1: mov     %w1, #0\n"          /* Failed */
        "2:\n"
        : "=&r"(tmp), "=r"(result)
        : "r"(&lock->locked), "r"(1)
        : "memory"
    );

    return result;
}

/**
 * @brief Release spin lock
 */
void smp_spinlock_unlock(smp_spinlock_t *lock)
{
    __asm__ volatile(
        "stlr   wzr, [%0]"              /* Store release zero */
        :
        : "r"(&lock->locked)
        : "memory"
    );
}
