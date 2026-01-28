/**
 * @file ekk_partition.h
 * @brief EK-KOR v2 - Network Partition Handling
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * ROJ Paper Section VI: Network Partition Resilience
 *
 * State machine for handling network partitions in distributed EK3 module clusters.
 * Key features:
 * - HEALTHY/SUSPECTING/MAJORITY/MINORITY/RECONCILING state machine
 * - Minority freeze mode (suspend election, droop-only power)
 * - Epoch-based reconciliation for rejoining partitions
 * - Power ramping at 10%/sec during reintegration
 *
 * NOVELTY: Autonomous partition detection and graceful degradation without
 * requiring external coordinator or network infrastructure changes.
 */

#ifndef EKK_PARTITION_H
#define EKK_PARTITION_H

#include "ekk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * PARTITION CONFIGURATION
 * ============================================================================ */

/**
 * @brief Time threshold for suspecting partition (30ms)
 *
 * If visible count drops below quorum for this duration, transition
 * from HEALTHY to SUSPECTING.
 */
#ifndef EKK_PARTITION_SUSPECT_TIMEOUT_US
#define EKK_PARTITION_SUSPECT_TIMEOUT_US    30000
#endif

/**
 * @brief Time to confirm partition state (100ms)
 *
 * After entering SUSPECTING, wait this duration before confirming
 * MAJORITY or MINORITY state.
 */
#ifndef EKK_PARTITION_CONFIRM_TIMEOUT_US
#define EKK_PARTITION_CONFIRM_TIMEOUT_US    100000
#endif

/**
 * @brief Power ramp rate: 10% per second (0.1/sec in Q16.16)
 *
 * During reconciliation, power ramps up at this rate.
 * 10% per second = full power in 10 seconds.
 */
#ifndef EKK_PARTITION_POWER_RAMP_RATE
#define EKK_PARTITION_POWER_RAMP_RATE       EKK_FLOAT_TO_FIXED(0.1f)
#endif

/**
 * @brief Maximum partition recovery time (10 seconds)
 *
 * Target metric: partition recovery should complete within 10s.
 */
#ifndef EKK_PARTITION_RECOVERY_MAX_US
#define EKK_PARTITION_RECOVERY_MAX_US       10000000
#endif

/* ============================================================================
 * PARTITION STATE
 * ============================================================================ */

/**
 * @brief Partition state machine states
 *
 * State transitions:
 *   HEALTHY -> SUSPECTING:  visible < quorum for SUSPECT_TIMEOUT
 *   SUSPECTING -> MAJORITY: visible >= quorum after CONFIRM_TIMEOUT
 *   SUSPECTING -> MINORITY: visible < quorum after CONFIRM_TIMEOUT
 *   MINORITY -> RECONCILING: higher epoch module becomes visible
 *   RECONCILING -> HEALTHY: state sync complete + power ramp complete
 *   MAJORITY -> HEALTHY: all expected modules visible again
 */
typedef enum {
    EKK_PARTITION_HEALTHY     = 0,  /**< Normal operation, quorum visible */
    EKK_PARTITION_SUSPECTING  = 1,  /**< Lost some modules, awaiting confirmation */
    EKK_PARTITION_MAJORITY    = 2,  /**< In majority partition, normal ops */
    EKK_PARTITION_MINORITY    = 3,  /**< In minority partition, freeze mode */
    EKK_PARTITION_RECONCILING = 4,  /**< Merging partitions, syncing state */
} ekk_partition_state_t;

/**
 * @brief Convert partition state to string
 */
const char* ekk_partition_state_str(ekk_partition_state_t state);

/**
 * @brief Partition context
 *
 * Tracks current partition state, visibility counts, and reconciliation progress.
 */
typedef struct {
    /* Current state */
    ekk_partition_state_t state;        /**< Current partition state */
    uint32_t epoch;                     /**< Monotonic partition counter */

    /* Visibility tracking */
    uint32_t total_modules;             /**< Total expected modules in cluster */
    uint32_t visible_count;             /**< Currently visible modules */
    uint32_t quorum_size;               /**< Required for majority (N/2 + 1) */

    /* Timing */
    ekk_time_us_t state_enter_time;     /**< When current state was entered */
    ekk_time_us_t last_tick;            /**< Last tick timestamp */
    ekk_time_us_t suspecting_start;     /**< When suspecting began */

    /* Power management during reconciliation */
    ekk_fixed_t power_ramp_factor;      /**< 0.0-1.0, ramps at 10%/sec */
    ekk_fixed_t target_power_factor;    /**< Target power (usually 1.0) */

    /* Freeze mode (minority partition) */
    bool freeze_active;                 /**< True when in minority partition */

    /* Reconciliation tracking */
    uint32_t sync_received_count;       /**< State sync messages received */
    uint32_t sync_expected_count;       /**< Expected sync messages */
    bool sync_complete;                 /**< True when state sync done */

    /* Highest epoch seen from other modules */
    uint32_t max_observed_epoch;        /**< Used for reconciliation */
} ekk_partition_ctx_t;

/* ============================================================================
 * PARTITION API
 * ============================================================================ */

/**
 * @brief Initialize partition context
 *
 * @param ctx Partition context (caller-allocated)
 * @param total_modules Total modules in cluster
 * @return EKK_OK on success
 */
ekk_error_t ekk_partition_init(ekk_partition_ctx_t *ctx, uint32_t total_modules);

/**
 * @brief Periodic tick - update state machine
 *
 * Call from main loop at regular intervals (e.g., every 10ms).
 * Handles state transitions and power ramping.
 *
 * @param ctx Partition context
 * @param now Current timestamp in microseconds
 * @return EKK_OK on success
 */
ekk_error_t ekk_partition_tick(ekk_partition_ctx_t *ctx, ekk_time_us_t now);

/**
 * @brief Update visibility count
 *
 * Called when heartbeat layer detects module visibility changes.
 *
 * @param ctx Partition context
 * @param visible_count Number of currently visible modules
 * @return EKK_OK on success
 */
ekk_error_t ekk_partition_on_visibility_change(ekk_partition_ctx_t *ctx,
                                                 uint32_t visible_count);

/**
 * @brief Notify of epoch change from another module
 *
 * Called when receiving a message with epoch > our epoch.
 * May trigger transition to RECONCILING state.
 *
 * @param ctx Partition context
 * @param remote_epoch Epoch from remote module
 * @param remote_id Remote module ID
 * @return EKK_OK on success
 */
ekk_error_t ekk_partition_on_epoch_received(ekk_partition_ctx_t *ctx,
                                              uint32_t remote_epoch,
                                              ekk_module_id_t remote_id);

/**
 * @brief Get current power factor
 *
 * Returns power scaling factor based on partition state:
 * - HEALTHY/MAJORITY: 1.0 (full power)
 * - MINORITY: 0.0 (freeze - droop mode only)
 * - RECONCILING: ramping 0.0 -> 1.0 at 10%/sec
 *
 * @param ctx Partition context
 * @return Power factor (Q16.16, 0.0-1.0)
 */
ekk_fixed_t ekk_partition_get_power_factor(const ekk_partition_ctx_t *ctx);

/**
 * @brief Check if voting is allowed
 *
 * Minority partition modules should not participate in votes
 * to avoid split-brain scenarios.
 *
 * @param ctx Partition context
 * @return true if voting allowed, false in minority/reconciling
 */
bool ekk_partition_can_vote(const ekk_partition_ctx_t *ctx);

/**
 * @brief Check if election is allowed
 *
 * Minority partition modules should not start leader elections.
 *
 * @param ctx Partition context
 * @return true if election allowed, false in minority/reconciling
 */
bool ekk_partition_can_elect(const ekk_partition_ctx_t *ctx);

/**
 * @brief Signal state sync received during reconciliation
 *
 * @param ctx Partition context
 * @param from_id Module that sent state sync
 * @return EKK_OK on success
 */
ekk_error_t ekk_partition_on_sync_received(ekk_partition_ctx_t *ctx,
                                            ekk_module_id_t from_id);

/**
 * @brief Get current epoch
 *
 * @param ctx Partition context
 * @return Current epoch number
 */
static inline uint32_t ekk_partition_get_epoch(const ekk_partition_ctx_t *ctx) {
    return ctx->epoch;
}

/**
 * @brief Get current partition state
 */
static inline ekk_partition_state_t ekk_partition_get_state(const ekk_partition_ctx_t *ctx) {
    return ctx->state;
}

/**
 * @brief Check if in freeze mode (minority partition)
 */
static inline bool ekk_partition_is_frozen(const ekk_partition_ctx_t *ctx) {
    return ctx->freeze_active;
}

/* ============================================================================
 * PARTITION MESSAGE FORMAT
 * ============================================================================ */

/**
 * @brief Partition status message (included in heartbeat)
 *
 * Modules include their epoch and partition state in heartbeats
 * to enable epoch-based reconciliation.
 */
EKK_PACK_BEGIN
typedef struct {
    uint32_t epoch;                     /**< Module's current epoch */
    uint8_t state;                      /**< ekk_partition_state_t */
    uint8_t visible_count;              /**< Number of visible neighbors */
    uint8_t reserved[2];                /**< Alignment padding */
} EKK_PACKED ekk_partition_status_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_partition_status_t) == 8, "Partition status must be 8 bytes");

/**
 * @brief State sync request message
 *
 * Sent during reconciliation to request state from higher-epoch modules.
 */
EKK_PACK_BEGIN
typedef struct {
    uint32_t requesting_epoch;          /**< Requester's current epoch */
    ekk_module_id_t requester_id;       /**< Who is requesting */
    uint8_t reserved[3];
} EKK_PACKED ekk_sync_request_t;
EKK_PACK_END

/**
 * @brief State sync response message
 *
 * Response containing critical state to sync during reconciliation.
 */
EKK_PACK_BEGIN
typedef struct {
    uint32_t epoch;                     /**< Responder's epoch */
    uint32_t leader_term;               /**< Current Raft term (if leader elected) */
    ekk_module_id_t current_leader;     /**< Current cluster leader */
    uint8_t reserved[3];
} EKK_PACKED ekk_sync_response_t;
EKK_PACK_END

#ifdef __cplusplus
}
#endif

#endif /* EKK_PARTITION_H */
