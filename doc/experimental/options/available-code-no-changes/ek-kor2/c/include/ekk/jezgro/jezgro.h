/**
 * @file jezgro.h
 * @brief JEZGRO Microkernel - Master Include File
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license AGPL-3.0
 *
 * JEZGRO is a minimalist microkernel designed for safety-critical
 * embedded systems with the following properties:
 *
 * 1. HYBRID PRIVILEGE MODEL
 *    - Kernel space: LLC control, safety-critical code (privileged)
 *    - User space: ROJ coordination, diagnostics (unprivileged)
 *    - MPU-enforced isolation between services
 *
 * 2. MESSAGE-PASSING IPC
 *    - Zero-copy 64-byte messages (fits CAN-FD frame)
 *    - Synchronous call/reply pattern
 *    - Async notifications for events
 *    - Interrupt forwarding to user services
 *
 * 3. REINCARNATION SERVER
 *    - Monitors service health via heartbeats
 *    - Auto-restarts crashed services < 50ms
 *    - Exponential backoff for repeated crashes
 *    - Critical service detection
 *
 * DESIGN PHILOSOPHY:
 * - Minimal trusted computing base
 * - Fail-safe defaults
 * - Defense in depth
 * - Explicit over implicit
 *
 * PAPER REFERENCE:
 * ROJ Paper Section IV.B - JEZGRO Microkernel
 *
 * USAGE:
 * @code
 * #include <ekk/jezgro/jezgro.h>
 *
 * // Define LLC control service (kernel privilege)
 * static void llc_control_entry(void) {
 *     while (1) {
 *         // Safety-critical LLC control loop
 *         jezgro_rs_heartbeat(2);  // Signal alive
 *     }
 * }
 *
 * // Define ROJ coordination service (user privilege)
 * static void roj_coordination_entry(void) {
 *     while (1) {
 *         jezgro_ipc_msg_t msg;
 *         if (jezgro_ipc_recv(&msg, 100) == JEZGRO_IPC_OK) {
 *             // Handle coordination message
 *         }
 *         jezgro_rs_heartbeat(3);  // Signal alive
 *     }
 * }
 *
 * int main(void) {
 *     // Initialize JEZGRO subsystems
 *     jezgro_init();
 *
 *     // Register services
 *     jezgro_service_desc_t llc_desc = {
 *         .id = 2,
 *         .name = "llc-control",
 *         .priv = JEZGRO_PRIV_KERNEL,
 *         .entry = llc_control_entry,
 *         // ... memory regions
 *     };
 *     jezgro_rs_register_service(&llc_desc, true, true);
 *
 *     jezgro_service_desc_t roj_desc = {
 *         .id = 3,
 *         .name = "roj-coord",
 *         .priv = JEZGRO_PRIV_USER,
 *         .entry = roj_coordination_entry,
 *         // ... memory regions
 *     };
 *     jezgro_rs_register_service(&roj_desc, false, true);
 *
 *     // Start services
 *     jezgro_rs_start_service(2);
 *     jezgro_rs_start_service(3);
 *
 *     // Main loop
 *     while (1) {
 *         uint32_t now = ekk_hal_time_ms();
 *         jezgro_rs_tick(now);
 *     }
 * }
 * @endcode
 */

#ifndef JEZGRO_H
#define JEZGRO_H

/* Core type definitions */
#include "../ekk_types.h"

/* Hardware abstraction */
#include "../ekk_hal.h"

/* MPU isolation */
#include "jezgro_mpu.h"

/* Inter-process communication */
#include "jezgro_ipc.h"

/* Reincarnation server */
#include "jezgro_reincarnation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VERSION
 * ============================================================================ */

#define JEZGRO_VERSION_MAJOR    1
#define JEZGRO_VERSION_MINOR    0
#define JEZGRO_VERSION_PATCH    0
#define JEZGRO_VERSION_STRING   "1.0.0"

/**
 * @brief Get JEZGRO version as packed integer
 */
static inline uint32_t jezgro_version(void)
{
    return (JEZGRO_VERSION_MAJOR << 16) |
           (JEZGRO_VERSION_MINOR << 8) |
           JEZGRO_VERSION_PATCH;
}

/* ============================================================================
 * SYSTEM INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize all JEZGRO subsystems
 *
 * Initializes MPU, IPC, and Reincarnation Server.
 * Call once at startup before registering services.
 *
 * @return EKK_OK on success
 */
static inline ekk_error_t jezgro_init(void)
{
    ekk_error_t err;

    err = jezgro_mpu_init();
    if (err != EKK_OK && err != EKK_ERR_NOT_SUPPORTED) {
        return err;
    }

    err = jezgro_ipc_init();
    if (err != EKK_OK) {
        return err;
    }

    err = jezgro_rs_init();
    if (err != EKK_OK) {
        return err;
    }

    return EKK_OK;
}

/**
 * @brief Start JEZGRO kernel
 *
 * Enables MPU protection and starts the reincarnation server.
 */
static inline void jezgro_start(void)
{
    if (jezgro_mpu_available()) {
        jezgro_mpu_enable();
    }
    jezgro_rs_start();
}

/**
 * @brief Stop JEZGRO kernel
 */
static inline void jezgro_stop(void)
{
    jezgro_rs_stop();
    jezgro_mpu_disable();
}

/* ============================================================================
 * CONVENIENCE MACROS
 * ============================================================================ */

/**
 * @brief Define a kernel-privileged service
 */
#define JEZGRO_DEFINE_KERNEL_SERVICE(name, id, entry_fn, code_start, code_size, data_start, data_size, stack_start, stack_size) \
    static const jezgro_service_desc_t name##_desc = { \
        .id = (id), \
        .name = #name, \
        .priv = JEZGRO_PRIV_KERNEL, \
        .code_start = (void*)(code_start), \
        .code_size = (code_size), \
        .data_start = (void*)(data_start), \
        .data_size = (data_size), \
        .stack_start = (void*)(stack_start), \
        .stack_size = (stack_size), \
        .entry = (entry_fn), \
    }

/**
 * @brief Define a user-privileged service
 */
#define JEZGRO_DEFINE_USER_SERVICE(name, id, entry_fn, code_start, code_size, data_start, data_size, stack_start, stack_size) \
    static const jezgro_service_desc_t name##_desc = { \
        .id = (id), \
        .name = #name, \
        .priv = JEZGRO_PRIV_USER, \
        .code_start = (void*)(code_start), \
        .code_size = (code_size), \
        .data_start = (void*)(data_start), \
        .data_size = (data_size), \
        .stack_start = (void*)(stack_start), \
        .stack_size = (stack_size), \
        .entry = (entry_fn), \
    }

/* ============================================================================
 * DEBUG
 * ============================================================================ */

/**
 * @brief Dump complete JEZGRO state to debug output
 */
static inline void jezgro_dump_state(void)
{
    ekk_hal_printf("\n========== JEZGRO State ==========\n");
    ekk_hal_printf("Version: %s\n", JEZGRO_VERSION_STRING);

    jezgro_mpu_dump_config();
    jezgro_ipc_dump_state();
    jezgro_rs_dump_state();

    ekk_hal_printf("==================================\n\n");
}

#ifdef __cplusplus
}
#endif

#endif /* JEZGRO_H */
