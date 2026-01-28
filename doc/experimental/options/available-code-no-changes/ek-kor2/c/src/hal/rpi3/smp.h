/**
 * @file smp.h
 * @brief SMP (Symmetric Multi-Processing) Header for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_SMP_H
#define RPI3_SMP_H

#include <stdint.h>

/* ============================================================================
 * Core Identification
 * ============================================================================ */

/**
 * @brief Get current core ID
 * @return Core ID (0-3)
 */
uint32_t smp_get_core_id(void);

/**
 * @brief Get number of cores
 * @return Number of cores (4 on Pi 3)
 */
uint32_t smp_get_num_cores(void);

/* ============================================================================
 * SMP Control
 * ============================================================================ */

/**
 * @brief Initialize SMP (called by core 0)
 */
void smp_init(void);

/**
 * @brief Start a secondary core
 * @param core_id Core to start (1-3)
 * @param entry Entry point function (receives core_id)
 * @return 0 on success, -1 on error
 */
int smp_start_core(uint32_t core_id, void (*entry)(uint32_t));

/**
 * @brief Start all secondary cores
 * @param entry Entry point function for all cores
 */
void smp_start_all_cores(void (*entry)(uint32_t));

/**
 * @brief Wait for a core to be ready
 * @param core_id Core to wait for (0-3)
 * @param timeout_us Timeout in microseconds (0 = infinite)
 * @return 0 if ready, -1 on timeout
 */
int smp_wait_core_ready(uint32_t core_id, uint32_t timeout_us);

/**
 * @brief Wait for all cores to be ready
 * @param timeout_us Timeout per core (0 = infinite)
 * @return Number of ready cores
 */
uint32_t smp_wait_all_cores_ready(uint32_t timeout_us);

/**
 * @brief Check if a core is ready
 * @param core_id Core to check (0-3)
 * @return Non-zero if ready
 */
int smp_is_core_ready(uint32_t core_id);

/* ============================================================================
 * Inter-Processor Interrupts (IPI)
 * ============================================================================ */

/**
 * @brief Send IPI to a specific core
 * @param core_id Target core (0-3)
 * @param sgi_id SGI number (0-15)
 */
void smp_send_ipi(uint32_t core_id, uint8_t sgi_id);

/**
 * @brief Send IPI to all other cores
 * @param sgi_id SGI number (0-15)
 */
void smp_send_ipi_all(uint8_t sgi_id);

/* ============================================================================
 * Spin Lock
 * ============================================================================ */

/**
 * @brief Spin lock structure
 */
typedef struct {
    volatile uint32_t locked;
} smp_spinlock_t;

/**
 * @brief Static initializer for spin lock
 */
#define SMP_SPINLOCK_INIT {0}

/**
 * @brief Initialize spin lock
 */
void smp_spinlock_init(smp_spinlock_t *lock);

/**
 * @brief Acquire spin lock (blocking)
 */
void smp_spinlock_lock(smp_spinlock_t *lock);

/**
 * @brief Try to acquire spin lock (non-blocking)
 * @return Non-zero if acquired
 */
int smp_spinlock_trylock(smp_spinlock_t *lock);

/**
 * @brief Release spin lock
 */
void smp_spinlock_unlock(smp_spinlock_t *lock);

#endif /* RPI3_SMP_H */
