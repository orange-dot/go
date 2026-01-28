/**
 * @file ekk_heartbeat.h
 * @brief EK-KOR v2 - Heartbeat and Liveness Detection
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * NOVELTY: Kernel-Integrated Failure Detection
 *
 * Unlike traditional RTOS where failure detection is application
 * responsibility, EK-KOR v2 integrates heartbeat monitoring into
 * the kernel. This enables:
 * - Automatic neighbor health tracking
 * - Immediate callback on neighbor loss
 * - Triggering of mesh reformation
 *
 * Part of: Adaptive Mesh Reformation
 *
 * PATENT CLAIMS (dependent):
 * - Adaptive mesh reformation upon node failure detection
 * - Heartbeat-based liveness with configurable timeout
 */

#ifndef EKK_HEARTBEAT_H
#define EKK_HEARTBEAT_H

#include "ekk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * HEARTBEAT CONFIGURATION
 * ============================================================================ */

/**
 * @brief Heartbeat configuration
 */
typedef struct {
    ekk_time_us_t period;           /**< Heartbeat send period */
    uint32_t timeout_count;         /**< Missed beats before failure */
    bool auto_broadcast;            /**< Automatically broadcast heartbeats */
    bool track_latency;             /**< Track RTT to neighbors */
} ekk_heartbeat_config_t;

#define EKK_HEARTBEAT_CONFIG_DEFAULT { \
    .period = EKK_HEARTBEAT_PERIOD_US, \
    .timeout_count = EKK_HEARTBEAT_TIMEOUT_COUNT, \
    .auto_broadcast = true, \
    .track_latency = false, \
}

/* ============================================================================
 * HEARTBEAT STATE
 * ============================================================================ */

/**
 * @brief Per-neighbor heartbeat tracking
 */
typedef struct {
    ekk_module_id_t id;             /**< Neighbor ID */
    ekk_health_state_t health;      /**< Current health state */
    ekk_time_us_t last_seen;        /**< Last heartbeat received */
    uint8_t missed_count;           /**< Consecutive missed heartbeats */
    uint8_t sequence;               /**< Last seen sequence number */
    ekk_time_us_t avg_latency;      /**< Average RTT (if tracking) */
} ekk_heartbeat_neighbor_t;

/**
 * @brief Heartbeat engine state
 */
typedef struct {
    ekk_module_id_t my_id;

    ekk_heartbeat_neighbor_t neighbors[EKK_MAX_MODULES];
    uint32_t neighbor_count;

    ekk_time_us_t last_send;        /**< Last heartbeat sent */
    uint8_t send_sequence;          /**< Outgoing sequence number */

    ekk_heartbeat_config_t config;

    /* Callbacks */
    void (*on_neighbor_alive)(ekk_module_id_t id);
    void (*on_neighbor_suspect)(ekk_module_id_t id);
    void (*on_neighbor_dead)(ekk_module_id_t id);
} ekk_heartbeat_t;

/* ============================================================================
 * HEARTBEAT API
 * ============================================================================ */

/**
 * @brief Initialize heartbeat engine
 *
 * @param hb Heartbeat state (caller-allocated)
 * @param my_id This module's ID
 * @param config Configuration (or NULL for defaults)
 * @return EKK_OK on success
 */
ekk_error_t ekk_heartbeat_init(ekk_heartbeat_t *hb,
                                ekk_module_id_t my_id,
                                const ekk_heartbeat_config_t *config);

/**
 * @brief Add neighbor to track
 *
 * @param hb Heartbeat state
 * @param neighbor_id Neighbor to track
 * @return EKK_OK on success
 */
ekk_error_t ekk_heartbeat_add_neighbor(ekk_heartbeat_t *hb,
                                        ekk_module_id_t neighbor_id);

/**
 * @brief Remove neighbor from tracking
 *
 * @param hb Heartbeat state
 * @param neighbor_id Neighbor to remove
 * @return EKK_OK on success
 */
ekk_error_t ekk_heartbeat_remove_neighbor(ekk_heartbeat_t *hb,
                                           ekk_module_id_t neighbor_id);

/**
 * @brief Process received heartbeat
 *
 * Called when heartbeat message received from neighbor.
 * Updates neighbor's health state.
 *
 * @param hb Heartbeat state
 * @param sender_id Sender's module ID
 * @param sequence Heartbeat sequence number
 * @param now Current timestamp
 * @return EKK_OK on success
 */
ekk_error_t ekk_heartbeat_received(ekk_heartbeat_t *hb,
                                    ekk_module_id_t sender_id,
                                    uint8_t sequence,
                                    ekk_time_us_t now);

/**
 * @brief Periodic tick
 *
 * Checks for timeouts and sends heartbeats if auto_broadcast enabled.
 *
 * @param hb Heartbeat state
 * @param now Current timestamp
 * @return Number of neighbors whose state changed
 */
uint32_t ekk_heartbeat_tick(ekk_heartbeat_t *hb, ekk_time_us_t now);

/**
 * @brief Send heartbeat now
 *
 * Manually trigger heartbeat broadcast.
 *
 * @param hb Heartbeat state
 * @return EKK_OK on success
 */
ekk_error_t ekk_heartbeat_send(ekk_heartbeat_t *hb);

/**
 * @brief Get neighbor health state
 *
 * @param hb Heartbeat state
 * @param neighbor_id Neighbor to query
 * @return Health state, or UNKNOWN if not tracked
 */
ekk_health_state_t ekk_heartbeat_get_health(const ekk_heartbeat_t *hb,
                                             ekk_module_id_t neighbor_id);

/**
 * @brief Get time since last heartbeat
 *
 * @param hb Heartbeat state
 * @param neighbor_id Neighbor to query
 * @return Microseconds since last seen, or UINT64_MAX if never seen
 */
ekk_time_us_t ekk_heartbeat_time_since(const ekk_heartbeat_t *hb,
                                        ekk_module_id_t neighbor_id);

/* ============================================================================
 * HEARTBEAT MESSAGE FORMAT
 * ============================================================================ */

/**
 * @brief Heartbeat message (broadcast periodically)
 */
EKK_PACK_BEGIN
typedef struct {
    uint8_t msg_type;               /**< EKK_MSG_HEARTBEAT */
    ekk_module_id_t sender_id;      /**< Sender's module ID */
    uint8_t sequence;               /**< Monotonic sequence */
    uint8_t state;                  /**< Sender's state (ekk_module_state_t) */
    uint8_t neighbor_count;         /**< Sender's neighbor count */
    uint8_t load_percent;           /**< Load 0-100% */
    uint8_t thermal_percent;        /**< Thermal 0-100% */
    uint8_t flags;                  /**< Reserved */
} EKK_PACKED ekk_heartbeat_msg_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_heartbeat_msg_t) == 8, "Heartbeat message wrong size");

/* ============================================================================
 * CALLBACKS
 * ============================================================================ */

/**
 * @brief Set callbacks for health state changes
 */
void ekk_heartbeat_set_callbacks(ekk_heartbeat_t *hb,
                                  void (*on_alive)(ekk_module_id_t),
                                  void (*on_suspect)(ekk_module_id_t),
                                  void (*on_dead)(ekk_module_id_t));

#ifdef __cplusplus
}
#endif

#endif /* EKK_HEARTBEAT_H */
