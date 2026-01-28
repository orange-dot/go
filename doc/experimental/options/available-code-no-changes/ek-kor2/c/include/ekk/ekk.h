/**
 * @file ekk.h
 * @brief EK-KOR v2 - Master Include File
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * EK-KOR v2: Field-Centric Coordination RTOS
 *
 * A novel real-time operating system designed for distributed coordination
 * of modular power electronics (EK3 charger modules).
 *
 * KEY INNOVATIONS:
 *
 * 1. POTENTIAL FIELD SCHEDULING
 *    No central scheduler. Modules publish decaying gradient fields;
 *    neighbors sample and self-organize based on field gradients.
 *    - Eliminates scheduler as bottleneck
 *    - Natural load balancing through gradient descent
 *    - Temporal scheduling via exponential decay
 *
 * 2. TOPOLOGICAL k-NEIGHBOR COORDINATION
 *    Each module tracks exactly k=7 logical neighbors regardless of
 *    physical topology. Enables scale-free coordination from 3 to 909 modules.
 *    - Based on Cavagna & Giardina (2010) starling research
 *    - Cohesion independent of density
 *    - Coordination waves propagate in <15ms
 *
 * 3. THRESHOLD CONSENSUS
 *    Distributed voting with density-dependent thresholds.
 *    Supermajority for safety-critical decisions; mutual inhibition
 *    for competing proposals.
 *    - No single point of failure
 *    - Byzantine fault tolerance
 *    - Weighted voting by health state
 *
 * 4. ADAPTIVE MESH REFORMATION
 *    Kernel-integrated heartbeat monitoring. Automatic neighbor
 *    reelection when modules fail. Self-healing topology.
 *    - Detection in 5 missed heartbeats (~50ms)
 *    - Reformation without reconfiguration
 *    - Graceful degradation to ISOLATED state
 *
 * USAGE:
 *
 * @code
 * #include <ekk/ekk.h>
 *
 * ekk_module_t my_module;
 *
 * int main(void) {
 *     ekk_hal_init();
 *
 *     ekk_position_t pos = {.x = 1, .y = 2, .z = 0};
 *     ekk_module_init(&my_module, 42, "charger-42", pos);
 *
 *     ekk_module_add_task(&my_module, "charge", charge_task, NULL, 0, 1000, NULL);
 *     ekk_module_add_task(&my_module, "thermal", thermal_task, NULL, 1, 5000, NULL);
 *
 *     ekk_module_start(&my_module);
 *
 *     while (1) {
 *         ekk_time_us_t now = ekk_hal_time_us();
 *         ekk_module_tick(&my_module, now);
 *     }
 * }
 * @endcode
 *
 * PATENT CLAIMS:
 *
 * 1. "A distributed real-time operating system using potential field
 *    scheduling wherein processing elements coordinate through indirect
 *    communication via shared decaying gradient fields"
 *
 * 2. "A topological coordination protocol for modular power electronics
 *    where each module maintains fixed-cardinality neighbor relationships
 *    independent of physical network topology"
 *
 * 3. "A threshold-based consensus mechanism for mixed-criticality embedded
 *    systems using density-dependent activation functions"
 *
 * REFERENCES:
 *
 * - Khatib, O. (1986): Real-time obstacle avoidance using potential fields
 * - Cavagna, A. & Giardina, I. (2010): Scale-free correlations in starlings
 * - Vicsek, T. (1995): Self-propelled particle model
 *
 * @author Elektrokombinacija
 * @date 2026
 */

#ifndef EKK_H
#define EKK_H

/* Core types and configuration */
#include "ekk_types.h"

/* Hardware abstraction */
#include "ekk_hal.h"

/* Coordination field engine */
#include "ekk_field.h"

/* Topological neighbor management */
#include "ekk_topology.h"

/* Threshold consensus */
#include "ekk_consensus.h"

/* Heartbeat and liveness */
#include "ekk_heartbeat.h"

/* Module - first class citizen */
#include "ekk_module.h"

/* SPSC ring buffer for lock-free IPC */
#include "ekk_spsc.h"

/* Chaskey MAC authentication */
#include "ekk_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VERSION
 * ============================================================================ */

#define EKK_VERSION_MAJOR   2
#define EKK_VERSION_MINOR   0
#define EKK_VERSION_PATCH   0
#define EKK_VERSION_STRING  "2.0.0"

/**
 * @brief Get version as packed integer (major << 16 | minor << 8 | patch)
 */
static inline uint32_t ekk_version(void)
{
    return (EKK_VERSION_MAJOR << 16) | (EKK_VERSION_MINOR << 8) | EKK_VERSION_PATCH;
}

/* ============================================================================
 * SYSTEM INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize EK-KOR v2 system
 *
 * Initializes HAL, field region, and internal state.
 * Call once at startup before creating modules.
 *
 * @return EKK_OK on success
 */
ekk_error_t ekk_init(void);

/**
 * @brief Get global field region
 *
 * Returns pointer to shared coordination field region.
 * Used internally by modules.
 */
ekk_field_region_t* ekk_get_field_region(void);

#ifdef __cplusplus
}
#endif

#endif /* EKK_H */
