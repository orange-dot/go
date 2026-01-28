/**
 * @file ekk_raft.h
 * @brief EK-KOR v2 - Raft Leader Election
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * ROJ Paper Section IV, Algorithm 1: Raft-based Leader Election
 *
 * Simplified Raft implementation for EK3 module leader election:
 * - FOLLOWER/CANDIDATE/LEADER states
 * - Randomized election timeouts (150-300ms)
 * - CAN-FD broadcast with implicit acknowledgment
 * - Term persistence in flash
 * - Election in <400ms (99th percentile)
 *
 * Key simplifications from full Raft:
 * - No log replication (only used for leader election)
 * - Integrates with ekk_partition for split-brain prevention
 * - Uses CAN-FD multicast instead of point-to-point RPC
 *
 * NOVELTY: Adapted Raft for CAN-FD bus topology with integrated
 * partition handling to prevent split-brain scenarios.
 */

#ifndef EKK_RAFT_H
#define EKK_RAFT_H

#include "ekk_types.h"
#include "ekk_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * RAFT CONFIGURATION
 * ============================================================================ */

/**
 * @brief Minimum election timeout (150ms)
 */
#ifndef EKK_RAFT_ELECTION_TIMEOUT_MIN_US
#define EKK_RAFT_ELECTION_TIMEOUT_MIN_US    150000
#endif

/**
 * @brief Maximum election timeout (300ms)
 */
#ifndef EKK_RAFT_ELECTION_TIMEOUT_MAX_US
#define EKK_RAFT_ELECTION_TIMEOUT_MAX_US    300000
#endif

/**
 * @brief Heartbeat interval (50ms)
 *
 * Leader sends heartbeats at this interval.
 * Must be << election timeout to prevent spurious elections.
 */
#ifndef EKK_RAFT_HEARTBEAT_INTERVAL_US
#define EKK_RAFT_HEARTBEAT_INTERVAL_US      50000
#endif

/**
 * @brief Maximum time for election completion (400ms)
 *
 * Target metric from paper: 99th percentile election < 400ms.
 */
#ifndef EKK_RAFT_ELECTION_MAX_US
#define EKK_RAFT_ELECTION_MAX_US            400000
#endif

/* ============================================================================
 * RAFT STATE
 * ============================================================================ */

/**
 * @brief Raft node states
 */
typedef enum {
    EKK_RAFT_FOLLOWER   = 0,    /**< Following a leader */
    EKK_RAFT_CANDIDATE  = 1,    /**< Running for leader */
    EKK_RAFT_LEADER     = 2,    /**< Elected leader */
} ekk_raft_state_t;

/**
 * @brief Convert Raft state to string
 */
const char* ekk_raft_state_str(ekk_raft_state_t state);

/**
 * @brief Raft context
 */
typedef struct {
    /* Identity */
    ekk_module_id_t my_id;              /**< This module's ID */

    /* Persistent state (survives reboot - saved to flash) */
    uint32_t current_term;              /**< Current election term */
    ekk_module_id_t voted_for;          /**< Who we voted for in current term */

    /* Volatile state */
    ekk_raft_state_t state;             /**< Current Raft state */
    ekk_module_id_t current_leader;     /**< Known leader (0 if unknown) */

    /* Timing */
    ekk_time_us_t election_timeout;     /**< Current randomized timeout */
    ekk_time_us_t last_heartbeat;       /**< Last heartbeat received/sent */
    ekk_time_us_t election_start;       /**< When election started */

    /* Election tracking */
    uint8_t votes_received;             /**< Votes received as candidate */
    uint8_t votes_needed;               /**< Votes needed for majority */
    uint8_t total_voters;               /**< Total eligible voters */
    bool vote_granted[EKK_MAX_MODULES]; /**< Track who voted for us */

    /* Callbacks */
    void (*on_become_leader)(void *user_data);
    void (*on_become_follower)(ekk_module_id_t leader, void *user_data);
    void (*on_leader_lost)(void *user_data);
    void *user_data;

    /* Flash persistence (function pointers set by HAL) */
    ekk_error_t (*persist_term)(uint32_t term, ekk_module_id_t voted_for);
    ekk_error_t (*restore_term)(uint32_t *term, ekk_module_id_t *voted_for);

    /* Partition integration */
    ekk_partition_ctx_t *partition_ctx; /**< For split-brain prevention */

    /* Random seed for timeout randomization */
    uint32_t rand_state;
} ekk_raft_ctx_t;

/* ============================================================================
 * RAFT API
 * ============================================================================ */

/**
 * @brief Initialize Raft context
 *
 * @param ctx Raft context (caller-allocated)
 * @param my_id This module's ID
 * @param total_modules Total modules in cluster
 * @param partition_ctx Partition context for split-brain prevention (can be NULL)
 * @return EKK_OK on success
 */
ekk_error_t ekk_raft_init(ekk_raft_ctx_t *ctx,
                           ekk_module_id_t my_id,
                           uint8_t total_modules,
                           ekk_partition_ctx_t *partition_ctx);

/**
 * @brief Periodic tick - run Raft state machine
 *
 * Call from main loop at regular intervals (e.g., every 10ms).
 * Handles election timeouts, heartbeats, and state transitions.
 *
 * @param ctx Raft context
 * @param now Current timestamp in microseconds
 * @return EKK_OK on success
 */
ekk_error_t ekk_raft_tick(ekk_raft_ctx_t *ctx, ekk_time_us_t now);

/**
 * @brief Handle received heartbeat (AppendEntries RPC)
 *
 * Called when receiving a heartbeat from leader.
 *
 * @param ctx Raft context
 * @param leader_id Leader's module ID
 * @param term Leader's term
 * @param now Current timestamp
 * @return EKK_OK on success
 */
ekk_error_t ekk_raft_on_heartbeat(ekk_raft_ctx_t *ctx,
                                    ekk_module_id_t leader_id,
                                    uint32_t term,
                                    ekk_time_us_t now);

/**
 * @brief Handle vote request (RequestVote RPC)
 *
 * Called when receiving a vote request from candidate.
 *
 * @param ctx Raft context
 * @param candidate_id Candidate's module ID
 * @param term Candidate's term
 * @param now Current timestamp
 * @return true if vote granted, false otherwise
 */
bool ekk_raft_on_vote_request(ekk_raft_ctx_t *ctx,
                               ekk_module_id_t candidate_id,
                               uint32_t term,
                               ekk_time_us_t now);

/**
 * @brief Handle vote response
 *
 * Called when receiving a vote response.
 *
 * @param ctx Raft context
 * @param voter_id Voter's module ID
 * @param term Voter's term
 * @param vote_granted Whether vote was granted
 * @param now Current timestamp
 * @return EKK_OK on success
 */
ekk_error_t ekk_raft_on_vote_response(ekk_raft_ctx_t *ctx,
                                        ekk_module_id_t voter_id,
                                        uint32_t term,
                                        bool vote_granted,
                                        ekk_time_us_t now);

/**
 * @brief Get current leader
 *
 * @param ctx Raft context
 * @return Current leader ID, or 0 if no known leader
 */
ekk_module_id_t ekk_raft_get_leader(const ekk_raft_ctx_t *ctx);

/**
 * @brief Check if this node is leader
 */
static inline bool ekk_raft_is_leader(const ekk_raft_ctx_t *ctx) {
    return ctx->state == EKK_RAFT_LEADER;
}

/**
 * @brief Get current term
 */
static inline uint32_t ekk_raft_get_term(const ekk_raft_ctx_t *ctx) {
    return ctx->current_term;
}

/**
 * @brief Get current state
 */
static inline ekk_raft_state_t ekk_raft_get_state(const ekk_raft_ctx_t *ctx) {
    return ctx->state;
}

/**
 * @brief Set persistence callbacks
 *
 * @param ctx Raft context
 * @param persist Function to persist term/vote to flash
 * @param restore Function to restore term/vote from flash
 */
void ekk_raft_set_persistence(ekk_raft_ctx_t *ctx,
                               ekk_error_t (*persist)(uint32_t, ekk_module_id_t),
                               ekk_error_t (*restore)(uint32_t*, ekk_module_id_t*));

/**
 * @brief Set leader callbacks
 *
 * @param ctx Raft context
 * @param on_leader Called when becoming leader
 * @param on_follower Called when becoming follower
 * @param on_lost Called when leader is lost
 * @param user_data User data passed to callbacks
 */
void ekk_raft_set_callbacks(ekk_raft_ctx_t *ctx,
                             void (*on_leader)(void*),
                             void (*on_follower)(ekk_module_id_t, void*),
                             void (*on_lost)(void*),
                             void *user_data);

/**
 * @brief Force step down from leader role
 *
 * Used for graceful shutdown or when partition detected.
 */
void ekk_raft_step_down(ekk_raft_ctx_t *ctx, ekk_time_us_t now);

/* ============================================================================
 * RAFT MESSAGE TYPES (for ekk_hal.h integration)
 * ============================================================================ */

/* Add these to ekk_msg_type_t enum */
#define EKK_MSG_RAFT_HEARTBEAT      0x10    /**< Leader heartbeat */
#define EKK_MSG_RAFT_REQUEST_VOTE   0x11    /**< Vote request from candidate */
#define EKK_MSG_RAFT_VOTE_RESPONSE  0x12    /**< Vote response */

/* ============================================================================
 * RAFT MESSAGE FORMATS
 * ============================================================================ */

/**
 * @brief Raft heartbeat message (simplified AppendEntries)
 *
 * Sent by leader at HEARTBEAT_INTERVAL to maintain leadership.
 */
EKK_PACK_BEGIN
typedef struct {
    uint32_t term;                      /**< Leader's term */
    ekk_module_id_t leader_id;          /**< Leader's ID */
    uint8_t reserved[3];
} EKK_PACKED ekk_raft_heartbeat_msg_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_raft_heartbeat_msg_t) == 8, "Heartbeat must be 8 bytes");

/**
 * @brief Raft vote request message (RequestVote RPC)
 *
 * Sent by candidate to request votes.
 */
EKK_PACK_BEGIN
typedef struct {
    uint32_t term;                      /**< Candidate's term */
    ekk_module_id_t candidate_id;       /**< Candidate's ID */
    uint8_t reserved[3];
} EKK_PACKED ekk_raft_vote_request_msg_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_raft_vote_request_msg_t) == 8, "Vote request must be 8 bytes");

/**
 * @brief Raft vote response message
 *
 * Response to vote request.
 */
EKK_PACK_BEGIN
typedef struct {
    uint32_t term;                      /**< Responder's term */
    ekk_module_id_t voter_id;           /**< Voter's ID */
    uint8_t vote_granted;               /**< 1 if granted, 0 if rejected */
    uint8_t reserved[2];
} EKK_PACKED ekk_raft_vote_response_msg_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_raft_vote_response_msg_t) == 8, "Vote response must be 8 bytes");

#ifdef __cplusplus
}
#endif

#endif /* EKK_RAFT_H */
