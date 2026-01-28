/**
 * @file ekk_raft.c
 * @brief EK-KOR v2 - Raft Leader Election Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * ROJ Paper Section IV, Algorithm 1: Raft-based Leader Election
 *
 * Implements simplified Raft for CAN-FD bus topology:
 * - Randomized election timeouts (150-300ms uniform distribution)
 * - Term persistence to flash
 * - Integration with partition handling for split-brain prevention
 * - Target: election < 400ms (99th percentile)
 */

#include "ekk/ekk_raft.h"
#include "ekk/ekk_hal.h"

/* ============================================================================
 * STATE STRING CONVERSION
 * ============================================================================ */

const char* ekk_raft_state_str(ekk_raft_state_t state) {
    switch (state) {
        case EKK_RAFT_FOLLOWER:  return "FOLLOWER";
        case EKK_RAFT_CANDIDATE: return "CANDIDATE";
        case EKK_RAFT_LEADER:    return "LEADER";
        default:                 return "UNKNOWN";
    }
}

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/**
 * @brief Simple xorshift32 PRNG for election timeout randomization
 */
static uint32_t raft_rand(ekk_raft_ctx_t *ctx) {
    uint32_t x = ctx->rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ctx->rand_state = x;
    return x;
}

/**
 * @brief Generate random election timeout in [MIN, MAX] range
 *
 * Uniform distribution over 150-300ms range.
 */
static ekk_time_us_t raft_random_timeout(ekk_raft_ctx_t *ctx) {
    uint32_t range = EKK_RAFT_ELECTION_TIMEOUT_MAX_US - EKK_RAFT_ELECTION_TIMEOUT_MIN_US;
    uint32_t offset = raft_rand(ctx) % range;
    return EKK_RAFT_ELECTION_TIMEOUT_MIN_US + offset;
}

/**
 * @brief Reset election timer with new random timeout
 */
static void raft_reset_election_timer(ekk_raft_ctx_t *ctx, ekk_time_us_t now) {
    ctx->election_timeout = raft_random_timeout(ctx);
    ctx->last_heartbeat = now;
}

/**
 * @brief Transition to follower state
 */
static void raft_become_follower(ekk_raft_ctx_t *ctx, uint32_t term, ekk_time_us_t now) {
    bool was_leader = (ctx->state == EKK_RAFT_LEADER);

    ctx->state = EKK_RAFT_FOLLOWER;
    ctx->current_term = term;
    ctx->voted_for = EKK_INVALID_MODULE_ID;

    raft_reset_election_timer(ctx, now);

    /* Persist new term */
    if (ctx->persist_term != NULL) {
        ctx->persist_term(ctx->current_term, ctx->voted_for);
    }

    /* Notify if stepping down from leader */
    if (was_leader && ctx->on_leader_lost != NULL) {
        ctx->on_leader_lost(ctx->user_data);
    }
}

/**
 * @brief Transition to candidate state and start election
 */
static void raft_become_candidate(ekk_raft_ctx_t *ctx, ekk_time_us_t now) {
    ctx->state = EKK_RAFT_CANDIDATE;
    ctx->current_term++;
    ctx->voted_for = ctx->my_id;  /* Vote for self */
    ctx->election_start = now;

    /* Reset vote tracking */
    ctx->votes_received = 1;  /* Already have our own vote */
    for (int i = 0; i < EKK_MAX_MODULES; i++) {
        ctx->vote_granted[i] = false;
    }
    ctx->vote_granted[ctx->my_id] = true;

    /* Persist new term and vote */
    if (ctx->persist_term != NULL) {
        ctx->persist_term(ctx->current_term, ctx->voted_for);
    }

    raft_reset_election_timer(ctx, now);

    /* Broadcast vote request */
    ekk_raft_vote_request_msg_t msg = {
        .term = ctx->current_term,
        .candidate_id = ctx->my_id,
    };
    ekk_hal_broadcast(EKK_MSG_RAFT_REQUEST_VOTE, &msg, sizeof(msg));
}

/**
 * @brief Transition to leader state
 */
static void raft_become_leader(ekk_raft_ctx_t *ctx, ekk_time_us_t now) {
    ctx->state = EKK_RAFT_LEADER;
    ctx->current_leader = ctx->my_id;
    ctx->last_heartbeat = now;

    /* Send immediate heartbeat to establish leadership */
    ekk_raft_heartbeat_msg_t msg = {
        .term = ctx->current_term,
        .leader_id = ctx->my_id,
    };
    ekk_hal_broadcast(EKK_MSG_RAFT_HEARTBEAT, &msg, sizeof(msg));

    /* Notify application */
    if (ctx->on_become_leader != NULL) {
        ctx->on_become_leader(ctx->user_data);
    }
}

/**
 * @brief Check if election allowed (partition-aware)
 */
static bool raft_can_start_election(ekk_raft_ctx_t *ctx) {
    if (ctx->partition_ctx != NULL) {
        return ekk_partition_can_elect(ctx->partition_ctx);
    }
    return true;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================ */

ekk_error_t ekk_raft_init(ekk_raft_ctx_t *ctx,
                           ekk_module_id_t my_id,
                           uint8_t total_modules,
                           ekk_partition_ctx_t *partition_ctx) {
    if (ctx == NULL || my_id == EKK_INVALID_MODULE_ID || total_modules == 0) {
        return EKK_ERR_INVALID_ARG;
    }

    ctx->my_id = my_id;

    /* Initialize persistent state */
    ctx->current_term = 0;
    ctx->voted_for = EKK_INVALID_MODULE_ID;

    /* Initialize volatile state */
    ctx->state = EKK_RAFT_FOLLOWER;
    ctx->current_leader = EKK_INVALID_MODULE_ID;

    ctx->election_timeout = 0;
    ctx->last_heartbeat = 0;
    ctx->election_start = 0;

    ctx->votes_received = 0;
    ctx->votes_needed = (total_modules / 2) + 1;
    ctx->total_voters = total_modules;

    for (int i = 0; i < EKK_MAX_MODULES; i++) {
        ctx->vote_granted[i] = false;
    }

    /* Callbacks */
    ctx->on_become_leader = NULL;
    ctx->on_become_follower = NULL;
    ctx->on_leader_lost = NULL;
    ctx->user_data = NULL;

    ctx->persist_term = NULL;
    ctx->restore_term = NULL;

    ctx->partition_ctx = partition_ctx;

    /* Initialize PRNG with module ID to ensure different seeds */
    ctx->rand_state = my_id * 1103515245 + 12345;

    /* Try to restore persisted state */
    if (ctx->restore_term != NULL) {
        uint32_t restored_term;
        ekk_module_id_t restored_vote;
        if (ctx->restore_term(&restored_term, &restored_vote) == EKK_OK) {
            ctx->current_term = restored_term;
            ctx->voted_for = restored_vote;
        }
    }

    return EKK_OK;
}

ekk_error_t ekk_raft_tick(ekk_raft_ctx_t *ctx, ekk_time_us_t now) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Initialize timing on first tick */
    if (ctx->last_heartbeat == 0) {
        raft_reset_election_timer(ctx, now);
        return EKK_OK;
    }

    ekk_time_us_t elapsed = now - ctx->last_heartbeat;

    switch (ctx->state) {
        case EKK_RAFT_FOLLOWER:
            /* Check for election timeout */
            if (elapsed >= ctx->election_timeout) {
                if (raft_can_start_election(ctx)) {
                    /* No heartbeat received - start election */
                    raft_become_candidate(ctx, now);
                } else {
                    /* Can't start election (minority partition) - just reset timer */
                    raft_reset_election_timer(ctx, now);
                }
            }
            break;

        case EKK_RAFT_CANDIDATE:
            /* Check if already have majority */
            if (ctx->votes_received >= ctx->votes_needed) {
                raft_become_leader(ctx, now);
            }
            /* Check for election timeout - restart election */
            else if (elapsed >= ctx->election_timeout) {
                if (raft_can_start_election(ctx)) {
                    /* Restart election with new term */
                    raft_become_candidate(ctx, now);
                } else {
                    /* Can't continue election - step down */
                    raft_become_follower(ctx, ctx->current_term, now);
                }
            }
            /* Check for overall election timeout */
            else if ((now - ctx->election_start) >= EKK_RAFT_ELECTION_MAX_US) {
                /* Election taking too long - step down */
                raft_become_follower(ctx, ctx->current_term, now);
            }
            break;

        case EKK_RAFT_LEADER:
            /* Send periodic heartbeats */
            if (elapsed >= EKK_RAFT_HEARTBEAT_INTERVAL_US) {
                ekk_raft_heartbeat_msg_t msg = {
                    .term = ctx->current_term,
                    .leader_id = ctx->my_id,
                };
                ekk_hal_broadcast(EKK_MSG_RAFT_HEARTBEAT, &msg, sizeof(msg));
                ctx->last_heartbeat = now;
            }

            /* Check if we should step down (partition) */
            if (ctx->partition_ctx != NULL) {
                if (!ekk_partition_can_elect(ctx->partition_ctx)) {
                    ekk_raft_step_down(ctx, now);
                }
            }
            break;
    }

    return EKK_OK;
}

ekk_error_t ekk_raft_on_heartbeat(ekk_raft_ctx_t *ctx,
                                    ekk_module_id_t leader_id,
                                    uint32_t term,
                                    ekk_time_us_t now) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* If term > current term, update and become follower */
    if (term > ctx->current_term) {
        raft_become_follower(ctx, term, now);
    }

    /* If valid heartbeat from current or new leader */
    if (term >= ctx->current_term) {
        ctx->current_leader = leader_id;

        if (ctx->state != EKK_RAFT_FOLLOWER) {
            /* Step down if we're candidate or leader */
            raft_become_follower(ctx, term, now);

            if (ctx->on_become_follower != NULL) {
                ctx->on_become_follower(leader_id, ctx->user_data);
            }
        } else {
            /* Reset election timer */
            raft_reset_election_timer(ctx, now);
        }
    }

    return EKK_OK;
}

bool ekk_raft_on_vote_request(ekk_raft_ctx_t *ctx,
                               ekk_module_id_t candidate_id,
                               uint32_t term,
                               ekk_time_us_t now) {
    if (ctx == NULL) {
        return false;
    }

    bool vote_granted = false;

    /* If term > current term, update and become follower */
    if (term > ctx->current_term) {
        raft_become_follower(ctx, term, now);
    }

    /* Grant vote if:
     * 1. Term matches current term
     * 2. Haven't voted or already voted for this candidate
     * 3. Partition allows voting
     */
    if (term == ctx->current_term) {
        bool can_vote = (ctx->voted_for == EKK_INVALID_MODULE_ID ||
                         ctx->voted_for == candidate_id);

        if (ctx->partition_ctx != NULL && !ekk_partition_can_vote(ctx->partition_ctx)) {
            can_vote = false;
        }

        if (can_vote) {
            ctx->voted_for = candidate_id;
            vote_granted = true;

            /* Persist vote */
            if (ctx->persist_term != NULL) {
                ctx->persist_term(ctx->current_term, ctx->voted_for);
            }

            /* Reset election timer (voter shouldn't start election) */
            raft_reset_election_timer(ctx, now);
        }
    }

    /* Send vote response */
    ekk_raft_vote_response_msg_t response = {
        .term = ctx->current_term,
        .voter_id = ctx->my_id,
        .vote_granted = vote_granted ? 1 : 0,
    };
    ekk_hal_send(candidate_id, EKK_MSG_RAFT_VOTE_RESPONSE, &response, sizeof(response));

    return vote_granted;
}

ekk_error_t ekk_raft_on_vote_response(ekk_raft_ctx_t *ctx,
                                        ekk_module_id_t voter_id,
                                        uint32_t term,
                                        bool vote_granted,
                                        ekk_time_us_t now) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Ignore if not candidate or wrong term */
    if (ctx->state != EKK_RAFT_CANDIDATE || term != ctx->current_term) {
        return EKK_OK;
    }

    /* If higher term seen, step down */
    if (term > ctx->current_term) {
        raft_become_follower(ctx, term, now);
        return EKK_OK;
    }

    /* Record vote */
    if (vote_granted && !ctx->vote_granted[voter_id]) {
        ctx->vote_granted[voter_id] = true;
        ctx->votes_received++;

        /* Check for majority */
        if (ctx->votes_received >= ctx->votes_needed) {
            raft_become_leader(ctx, now);
        }
    }

    return EKK_OK;
}

ekk_module_id_t ekk_raft_get_leader(const ekk_raft_ctx_t *ctx) {
    if (ctx == NULL) {
        return EKK_INVALID_MODULE_ID;
    }
    return ctx->current_leader;
}

void ekk_raft_set_persistence(ekk_raft_ctx_t *ctx,
                               ekk_error_t (*persist)(uint32_t, ekk_module_id_t),
                               ekk_error_t (*restore)(uint32_t*, ekk_module_id_t*)) {
    if (ctx != NULL) {
        ctx->persist_term = persist;
        ctx->restore_term = restore;
    }
}

void ekk_raft_set_callbacks(ekk_raft_ctx_t *ctx,
                             void (*on_leader)(void*),
                             void (*on_follower)(ekk_module_id_t, void*),
                             void (*on_lost)(void*),
                             void *user_data) {
    if (ctx != NULL) {
        ctx->on_become_leader = on_leader;
        ctx->on_become_follower = on_follower;
        ctx->on_leader_lost = on_lost;
        ctx->user_data = user_data;
    }
}

void ekk_raft_step_down(ekk_raft_ctx_t *ctx, ekk_time_us_t now) {
    if (ctx != NULL && ctx->state == EKK_RAFT_LEADER) {
        raft_become_follower(ctx, ctx->current_term, now);
    }
}
