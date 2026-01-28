/**
 * @file ekk_gossip.h
 * @brief EK-KOR v2 - Event Gossip Protocol for Federated Event Sourcing
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Epidemic gossip protocol for event synchronization between modules.
 * Based on version vectors for causal ordering and LWW for conflict resolution.
 *
 * KEY FEATURES:
 * - Version vectors for causal event ordering
 * - Last-Writer-Wins (LWW) conflict resolution
 * - k=7 neighbor gossip (matches EKK topology)
 * - Hop-limited propagation (TTL)
 * - Gap detection and recovery
 */

#ifndef EKK_GOSSIP_H
#define EKK_GOSSIP_H

#include "ekk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/** Maximum entries in version vector (one per possible module) */
#ifndef EKK_VV_MAX_ENTRIES
#define EKK_VV_MAX_ENTRIES          EKK_MAX_MODULES
#endif

/** Maximum hops for event propagation (TTL) */
#ifndef EKK_GOSSIP_MAX_HOPS
#define EKK_GOSSIP_MAX_HOPS         3
#endif

/** Gossip tick interval in microseconds */
#ifndef EKK_GOSSIP_TICK_US
#define EKK_GOSSIP_TICK_US          10000   /* 10ms */
#endif

/** Maximum events per gossip message */
#ifndef EKK_GOSSIP_BATCH_SIZE
#define EKK_GOSSIP_BATCH_SIZE       2
#endif

/** Anti-entropy sync interval in microseconds */
#ifndef EKK_GOSSIP_SYNC_INTERVAL_US
#define EKK_GOSSIP_SYNC_INTERVAL_US 10000000  /* 10 seconds */
#endif

/* ============================================================================
 * MESSAGE TYPES
 * ============================================================================ */

/** Event gossip batch message */
#define EKK_MSG_EVENT_GOSSIP        0x60

/** Event acknowledgment */
#define EKK_MSG_EVENT_ACK           0x61

/** Request for missing events (gap fill) */
#define EKK_MSG_EVENT_REQUEST       0x62

/** Full version vector sync */
#define EKK_MSG_VV_SYNC             0x63

/* ============================================================================
 * VERSION VECTOR
 * ============================================================================ */

/**
 * @brief Version vector entry
 *
 * Tracks the highest sequence number seen from each origin module.
 */
typedef struct {
    ekk_module_id_t module_id;      /**< Module this entry tracks */
    uint32_t sequence;              /**< Highest sequence seen from this module */
} ekk_vv_entry_t;

/**
 * @brief Version vector for causal ordering
 *
 * Sparse representation - only tracks modules we've seen events from.
 */
typedef struct {
    ekk_vv_entry_t entries[EKK_K_NEIGHBORS + 1];  /**< Compact storage */
    uint8_t count;                                 /**< Active entries */
} ekk_version_vector_t;

/**
 * @brief Version vector comparison result
 */
typedef enum {
    EKK_VV_EQUAL      = 0,   /**< Identical vectors */
    EKK_VV_BEFORE     = 1,   /**< Local happened-before remote */
    EKK_VV_AFTER      = 2,   /**< Remote happened-before local */
    EKK_VV_CONCURRENT = 3,   /**< Concurrent (neither dominates) */
} ekk_vv_order_t;

/* ============================================================================
 * EVENT V2 (Extended format with origin tracking)
 * ============================================================================ */

/**
 * @brief Extended event format for gossip protocol
 *
 * 36 bytes total (vs 32 bytes for v1 events).
 * Adds origin tracking for causality and deduplication.
 */
EKK_PACK_BEGIN
typedef struct EKK_PACKED {
    uint32_t sequence;              /**< Local sequence (for compat) */
    uint32_t timestamp_us;          /**< Event timestamp */
    uint8_t  event_type;            /**< Event type code */
    uint8_t  flags;                 /**< Event flags */
    uint8_t  origin_id;             /**< Original emitter module */
    uint8_t  hop_count;             /**< Hops from origin (TTL) */
    uint32_t origin_seq;            /**< Sequence at origin module */
    uint8_t  payload[20];           /**< Event payload */
} ekk_event_v2_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_event_v2_t) == 36, "Event v2 must be 36 bytes");

/* Event type codes */
#define EKK_EVENT_STATE_TRANSITION  0x01
#define EKK_EVENT_FIELD_PUBLISHED   0x02
#define EKK_EVENT_NEIGHBOR_JOINED   0x03
#define EKK_EVENT_NEIGHBOR_LEFT     0x04
#define EKK_EVENT_CONSENSUS_START   0x05
#define EKK_EVENT_CONSENSUS_VOTE    0x06
#define EKK_EVENT_CONSENSUS_RESULT  0x07
#define EKK_EVENT_USER_DEFINED      0x80

/* Event flags */
#define EKK_EVENT_FLAG_REQUIRES_ACK 0x01
#define EKK_EVENT_FLAG_PRIORITY     0x02
#define EKK_EVENT_FLAG_REPLAYED     0x04

/* ============================================================================
 * LWW TIMESTAMP (Last-Writer-Wins conflict resolution)
 * ============================================================================ */

/**
 * @brief Last-Writer-Wins timestamp for conflict resolution
 *
 * When concurrent writes occur, the one with higher timestamp wins.
 * Origin ID is used as deterministic tiebreaker when timestamps equal.
 */
typedef struct {
    uint32_t timestamp_us;          /**< Timestamp (higher wins) */
    uint8_t  origin_id;             /**< Tiebreaker (higher wins) */
} ekk_lww_timestamp_t;

/* ============================================================================
 * GOSSIP MESSAGES
 * ============================================================================ */

/**
 * @brief Compressed version vector summary (7 bytes)
 *
 * Compact VV for CAN-FD message space constraints.
 * Stores only sequence numbers for k=7 neighbors.
 */
typedef struct {
    uint8_t seqs[EKK_K_NEIGHBORS];  /**< Packed sequences (mod 256) */
} ekk_vv_summary_t;

/**
 * @brief Event gossip message
 *
 * Carries up to 2 events plus version vector summary.
 * Total: 1 + 1 + 1 + 7 + 72 = 82 bytes (fits in CAN-FD 64-byte with fragmentation)
 */
EKK_PACK_BEGIN
typedef struct EKK_PACKED {
    uint8_t msg_type;               /**< EKK_MSG_EVENT_GOSSIP */
    uint8_t source_module;          /**< Sender module ID */
    uint8_t event_count;            /**< Number of events (1-2) */
    ekk_vv_summary_t vv_summary;    /**< Compressed version vector */
    ekk_event_v2_t events[EKK_GOSSIP_BATCH_SIZE];
} ekk_gossip_msg_t;
EKK_PACK_END

/**
 * @brief Event acknowledgment message
 */
EKK_PACK_BEGIN
typedef struct EKK_PACKED {
    uint8_t msg_type;               /**< EKK_MSG_EVENT_ACK */
    uint8_t source_module;          /**< Acknowledging module */
    uint8_t ack_count;              /**< Number of sequences acked */
    uint32_t acked_seqs[4];         /**< Acknowledged origin sequences */
} ekk_gossip_ack_t;
EKK_PACK_END

/**
 * @brief Event request message (gap fill)
 */
EKK_PACK_BEGIN
typedef struct EKK_PACKED {
    uint8_t msg_type;               /**< EKK_MSG_EVENT_REQUEST */
    uint8_t requester;              /**< Requesting module */
    uint8_t target_origin;          /**< Origin module for events */
    uint8_t _reserved;
    uint32_t from_seq;              /**< First missing sequence */
    uint32_t to_seq;                /**< Last missing sequence */
} ekk_gossip_request_t;
EKK_PACK_END

/* ============================================================================
 * GOSSIP CONTEXT
 * ============================================================================ */

/**
 * @brief Gossip statistics
 */
typedef struct {
    uint32_t events_sent;           /**< Events sent via gossip */
    uint32_t events_received;       /**< Events received via gossip */
    uint32_t duplicates;            /**< Duplicate events filtered */
    uint32_t gaps_detected;         /**< Sequence gaps detected */
    uint32_t gaps_filled;           /**< Gaps successfully filled */
    uint32_t ttl_expired;           /**< Events dropped at hop limit */
} ekk_gossip_stats_t;

/**
 * @brief Per-neighbor gossip state
 */
typedef struct {
    ekk_module_id_t id;             /**< Neighbor module ID */
    uint32_t last_seen_seq;         /**< Last sequence seen from this neighbor */
    uint32_t cursor;                /**< Our sync cursor to this neighbor */
    ekk_time_us_t last_sync;        /**< Last anti-entropy sync time */
} ekk_neighbor_gossip_t;

/**
 * @brief Gossip protocol context
 */
typedef struct {
    ekk_module_id_t my_id;          /**< This module's ID */
    ekk_version_vector_t vv;        /**< Our version vector */
    uint32_t local_sequence;        /**< Next local sequence number */

    /* Neighbor tracking */
    ekk_neighbor_gossip_t neighbors[EKK_K_NEIGHBORS];
    uint8_t neighbor_count;

    /* Event buffer for outgoing gossip */
    ekk_event_v2_t pending_events[8];
    uint8_t pending_count;

    /* Gap detection buffer */
    ekk_event_v2_t buffered_events[4];
    uint8_t buffered_count;

    /* Timing */
    ekk_time_us_t last_gossip_tick;
    ekk_time_us_t last_sync;

    /* Statistics */
    ekk_gossip_stats_t stats;
} ekk_gossip_ctx_t;

/* ============================================================================
 * VERSION VECTOR API
 * ============================================================================ */

/**
 * @brief Initialize version vector
 */
void ekk_vv_init(ekk_version_vector_t *vv);

/**
 * @brief Get sequence for a module
 * @return Sequence number, or 0 if module not tracked
 */
uint32_t ekk_vv_get(const ekk_version_vector_t *vv, ekk_module_id_t module_id);

/**
 * @brief Set/update sequence for a module
 * @return EKK_OK on success, EKK_ERR_NO_MEMORY if VV full
 */
ekk_error_t ekk_vv_set(ekk_version_vector_t *vv, ekk_module_id_t module_id, uint32_t seq);

/**
 * @brief Increment local sequence
 */
uint32_t ekk_vv_increment(ekk_version_vector_t *vv, ekk_module_id_t module_id);

/**
 * @brief Compare two version vectors
 */
ekk_vv_order_t ekk_vv_compare(const ekk_version_vector_t *a, const ekk_version_vector_t *b);

/**
 * @brief Merge remote VV into local (element-wise max)
 */
void ekk_vv_merge(ekk_version_vector_t *local, const ekk_version_vector_t *remote);

/**
 * @brief Compress VV to summary (for messages)
 */
void ekk_vv_to_summary(const ekk_version_vector_t *vv, ekk_vv_summary_t *summary,
                       const ekk_module_id_t *neighbor_ids, uint8_t count);

/**
 * @brief Expand summary to VV
 */
void ekk_vv_from_summary(ekk_version_vector_t *vv, const ekk_vv_summary_t *summary,
                         const ekk_module_id_t *neighbor_ids, uint8_t count);

/* ============================================================================
 * LWW API
 * ============================================================================ */

/**
 * @brief Compare two LWW timestamps
 * @return true if a is newer than b
 */
bool ekk_lww_is_newer(ekk_lww_timestamp_t a, ekk_lww_timestamp_t b);

/**
 * @brief Create LWW timestamp from event
 */
ekk_lww_timestamp_t ekk_lww_from_event(const ekk_event_v2_t *event);

/* ============================================================================
 * GOSSIP PROTOCOL API
 * ============================================================================ */

/**
 * @brief Initialize gossip context
 */
ekk_error_t ekk_gossip_init(ekk_gossip_ctx_t *ctx, ekk_module_id_t my_id);

/**
 * @brief Add neighbor to gossip
 */
ekk_error_t ekk_gossip_add_neighbor(ekk_gossip_ctx_t *ctx, ekk_module_id_t neighbor_id);

/**
 * @brief Remove neighbor from gossip
 */
ekk_error_t ekk_gossip_remove_neighbor(ekk_gossip_ctx_t *ctx, ekk_module_id_t neighbor_id);

/**
 * @brief Emit local event (will be gossiped)
 */
ekk_error_t ekk_gossip_emit(ekk_gossip_ctx_t *ctx, uint8_t event_type,
                            const uint8_t *payload, uint8_t payload_len);

/**
 * @brief Periodic gossip tick - sends pending events to neighbors
 *
 * Call this from main loop every EKK_GOSSIP_TICK_US.
 * @param now Current timestamp in microseconds
 */
ekk_error_t ekk_gossip_tick(ekk_gossip_ctx_t *ctx, ekk_time_us_t now);

/**
 * @brief Handle received gossip message
 */
ekk_error_t ekk_gossip_handle_msg(ekk_gossip_ctx_t *ctx, const uint8_t *data, uint32_t len);

/**
 * @brief Handle received event from gossip
 *
 * Checks for duplicates, gaps, and hop limit.
 * @return EKK_OK if event is new and stored
 * @return EKK_ERR_ALREADY_EXISTS if duplicate
 * @return EKK_ERR_NOT_FOUND if gap detected (event buffered)
 */
ekk_error_t ekk_gossip_handle_event(ekk_gossip_ctx_t *ctx, const ekk_event_v2_t *event,
                                     ekk_module_id_t sender);

/**
 * @brief Trigger anti-entropy sync with a neighbor
 */
ekk_error_t ekk_gossip_sync(ekk_gossip_ctx_t *ctx, ekk_module_id_t neighbor_id);

/**
 * @brief Get gossip statistics
 */
void ekk_gossip_get_stats(const ekk_gossip_ctx_t *ctx, ekk_gossip_stats_t *stats);

/**
 * @brief Reset gossip statistics
 */
void ekk_gossip_reset_stats(ekk_gossip_ctx_t *ctx);

/* ============================================================================
 * CALLBACKS (implement in application)
 * ============================================================================ */

/**
 * @brief Callback to send gossip message
 * @note Weak - implement in application
 */
EKK_WEAK ekk_error_t ekk_gossip_send(ekk_module_id_t dest, const uint8_t *data, uint32_t len);

/**
 * @brief Callback when new event received
 * @note Weak - implement in application
 */
EKK_WEAK void ekk_gossip_on_event(const ekk_event_v2_t *event);

/**
 * @brief Callback to store event persistently
 * @note Weak - implement in application
 */
EKK_WEAK ekk_error_t ekk_gossip_store_event(const ekk_event_v2_t *event);

/**
 * @brief Callback to retrieve event by origin+seq
 * @note Weak - implement in application
 */
EKK_WEAK ekk_error_t ekk_gossip_load_event(ekk_module_id_t origin, uint32_t seq,
                                            ekk_event_v2_t *event);

#ifdef __cplusplus
}
#endif

#endif /* EKK_GOSSIP_H */
