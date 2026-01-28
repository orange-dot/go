/**
 * @file ekk_consensus.c
 * @brief EK-KOR v2 - Threshold-Based Distributed Consensus Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 *
 * NOVELTY: Threshold Consensus with Mutual Inhibition
 * - Density-dependent threshold voting
 * - Supermajority support for safety-critical decisions
 * - Mutual inhibition for competing proposals
 */

#include "ekk/ekk_consensus.h"
#include "ekk/ekk_hal.h"
#include <string.h>

/* ============================================================================
 * PRIVATE STATE
 * ============================================================================ */

/** Decision callback (application decides how to vote) */
static ekk_consensus_decide_cb g_decide_callback = NULL;

/** Completion callback (application notified of results) */
static ekk_consensus_complete_cb g_complete_callback = NULL;

/* ============================================================================
 * PRIVATE HELPERS
 * ============================================================================ */

/**
 * @brief Find ballot by ID
 * @return Index if found, -1 otherwise
 */
static int find_ballot_index(const ekk_consensus_t *cons, ekk_ballot_id_t id)
{
    for (uint32_t i = 0; i < cons->active_ballot_count; i++) {
        if (cons->ballots[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Check if a ballot is inhibited
 */
static bool is_inhibited(const ekk_consensus_t *cons, ekk_ballot_id_t ballot_id,
                          ekk_time_us_t now)
{
    for (uint32_t i = 0; i < cons->inhibit_count; i++) {
        if (cons->inhibited[i] == ballot_id && cons->inhibit_until[i] > now) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Allocate a free ballot slot
 * @return Index of free slot, -1 if none available
 */
static int allocate_ballot_slot(ekk_consensus_t *cons)
{
    if (cons->active_ballot_count >= EKK_MAX_BALLOTS) {
        return -1;
    }

    return (int)cons->active_ballot_count;
}

/**
 * @brief Evaluate ballot result based on votes and threshold
 */
static ekk_vote_result_t evaluate_ballot(const ekk_ballot_t *ballot,
                                          uint32_t neighbor_count)
{
    if (ballot->completed) {
        return ballot->result;
    }

    /* Check for inhibition (handled in tick) */
    /* Here we just evaluate vote counts */

    uint32_t total_votes = ballot->vote_count;
    uint32_t yes_votes = ballot->yes_count;
    uint32_t no_votes = ballot->no_count;

    /* If not all votes received, still pending */
    if (total_votes < neighbor_count) {
        /* However, check if approval is already impossible */
        uint32_t remaining = neighbor_count - total_votes;

        /* Best case: all remaining vote yes */
        uint32_t max_yes = yes_votes + remaining;
        ekk_fixed_t max_ratio = (neighbor_count > 0) ?
            (ekk_fixed_t)(((int64_t)max_yes << 16) / neighbor_count) : 0;

        if (max_ratio < ballot->threshold) {
            /* Even if all remaining vote yes, cannot reach threshold */
            return EKK_VOTE_REJECTED;
        }

        /* Check if already reached threshold */
        ekk_fixed_t current_ratio = (neighbor_count > 0) ?
            (ekk_fixed_t)(((int64_t)yes_votes << 16) / neighbor_count) : 0;

        if (current_ratio >= ballot->threshold) {
            /* Threshold reached! */
            return EKK_VOTE_APPROVED;
        }

        /* Still pending */
        return EKK_VOTE_PENDING;
    }

    /* All votes received, compute final result */
    ekk_fixed_t approval_ratio = (neighbor_count > 0) ?
        (ekk_fixed_t)(((int64_t)yes_votes << 16) / neighbor_count) : 0;

    if (approval_ratio >= ballot->threshold) {
        return EKK_VOTE_APPROVED;
    } else {
        return EKK_VOTE_REJECTED;
    }
}

/**
 * @brief Finalize a ballot with a result
 */
static void finalize_ballot(ekk_consensus_t *cons, ekk_ballot_t *ballot,
                             ekk_vote_result_t result)
{
    ballot->result = result;
    ballot->completed = true;

    /* Invoke completion callback */
    if (g_complete_callback != NULL) {
        g_complete_callback(cons, ballot, result);
    }
}

/**
 * @brief Remove completed ballots from active list
 */
static void cleanup_completed_ballots(ekk_consensus_t *cons)
{
    uint32_t write_idx = 0;

    for (uint32_t read_idx = 0; read_idx < cons->active_ballot_count; read_idx++) {
        if (!cons->ballots[read_idx].completed) {
            if (write_idx != read_idx) {
                cons->ballots[write_idx] = cons->ballots[read_idx];
            }
            write_idx++;
        }
    }

    cons->active_ballot_count = write_idx;
}

/**
 * @brief Broadcast proposal to neighbors
 */
static ekk_error_t broadcast_proposal(const ekk_consensus_t *cons,
                                       const ekk_ballot_t *ballot)
{
    ekk_proposal_msg_t msg = {
        .msg_type = EKK_MSG_PROPOSAL,
        .proposer_id = cons->my_id,
        .ballot_id = ballot->id,
        .type = ballot->type,
        .data = ballot->proposal_data,
        .threshold = ballot->threshold,
    };

    return ekk_hal_broadcast(EKK_MSG_PROPOSAL, &msg, sizeof(msg));
}

/**
 * @brief Send vote to proposer
 */
static ekk_error_t send_vote(const ekk_consensus_t *cons,
                              ekk_module_id_t proposer_id,
                              ekk_ballot_id_t ballot_id,
                              ekk_vote_value_t vote)
{
    ekk_vote_msg_t msg = {
        .msg_type = EKK_MSG_VOTE,
        .voter_id = cons->my_id,
        .ballot_id = ballot_id,
        .vote = vote,
        .timestamp = (uint32_t)(ekk_hal_time_us() & 0xFFFFFFFF),
    };

    return ekk_hal_send(proposer_id, EKK_MSG_VOTE, &msg, sizeof(msg));
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

ekk_error_t ekk_consensus_init(ekk_consensus_t *cons,
                                ekk_module_id_t my_id,
                                const ekk_consensus_config_t *config)
{
    if (cons == NULL || my_id == EKK_INVALID_MODULE_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    memset(cons, 0, sizeof(ekk_consensus_t));
    cons->my_id = my_id;
    cons->next_ballot_id = 1;  /* 0 is invalid */

    /* Apply configuration */
    if (config != NULL) {
        cons->config = *config;
    } else {
        ekk_consensus_config_t default_config = EKK_CONSENSUS_CONFIG_DEFAULT;
        cons->config = default_config;
    }

    return EKK_OK;
}

/* ============================================================================
 * PROPOSAL CREATION
 * ============================================================================ */

ekk_error_t ekk_consensus_propose(ekk_consensus_t *cons,
                                   ekk_proposal_type_t type,
                                   uint32_t data,
                                   ekk_fixed_t threshold,
                                   ekk_ballot_id_t *ballot_id)
{
    if (cons == NULL || ballot_id == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Allocate ballot slot */
    int idx = allocate_ballot_slot(cons);
    if (idx < 0) {
        return EKK_ERR_BUSY;
    }

    ekk_time_us_t now = ekk_hal_time_us();

    /* Initialize ballot */
    ekk_ballot_t *ballot = &cons->ballots[idx];
    memset(ballot, 0, sizeof(ekk_ballot_t));

    ballot->id = cons->next_ballot_id++;
    ballot->type = type;
    ballot->proposer = cons->my_id;
    ballot->proposal_data = data;
    ballot->threshold = threshold;
    ballot->deadline = now + cons->config.vote_timeout;
    ballot->result = EKK_VOTE_PENDING;
    ballot->completed = false;

    /* Initialize vote tracking */
    memset(ballot->votes, EKK_VOTE_ABSTAIN, sizeof(ballot->votes));

    /* Self-vote if allowed */
    if (cons->config.allow_self_vote) {
        /* Proposer implicitly votes yes for own proposal */
        ballot->votes[0] = EKK_VOTE_YES;
        ballot->vote_count = 1;
        ballot->yes_count = 1;
    }

    cons->active_ballot_count++;

    /* Broadcast proposal */
    broadcast_proposal(cons, ballot);

    *ballot_id = ballot->id;
    return EKK_OK;
}

/* ============================================================================
 * VOTING
 * ============================================================================ */

ekk_error_t ekk_consensus_vote(ekk_consensus_t *cons,
                                ekk_ballot_id_t ballot_id,
                                ekk_vote_value_t vote)
{
    if (cons == NULL || ballot_id == EKK_INVALID_BALLOT_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Find ballot */
    int idx = find_ballot_index(cons, ballot_id);
    if (idx < 0) {
        return EKK_ERR_NOT_FOUND;
    }

    ekk_ballot_t *ballot = &cons->ballots[idx];

    /* Cannot vote on completed ballot */
    if (ballot->completed) {
        return EKK_ERR_BUSY;
    }

    /* Send vote to proposer */
    return send_vote(cons, ballot->proposer, ballot_id, vote);
}

/* ============================================================================
 * INHIBITION
 * ============================================================================ */

ekk_error_t ekk_consensus_inhibit(ekk_consensus_t *cons,
                                   ekk_ballot_id_t ballot_id)
{
    if (cons == NULL || ballot_id == EKK_INVALID_BALLOT_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Check if already inhibited */
    ekk_time_us_t now = ekk_hal_time_us();
    for (uint32_t i = 0; i < cons->inhibit_count; i++) {
        if (cons->inhibited[i] == ballot_id) {
            /* Update expiry */
            cons->inhibit_until[i] = now + cons->config.inhibit_duration;
            return EKK_OK;
        }
    }

    /* Add new inhibition */
    if (cons->inhibit_count >= EKK_MAX_BALLOTS) {
        /* Evict oldest */
        for (uint32_t i = 0; i < cons->inhibit_count - 1; i++) {
            cons->inhibited[i] = cons->inhibited[i + 1];
            cons->inhibit_until[i] = cons->inhibit_until[i + 1];
        }
        cons->inhibit_count--;
    }

    cons->inhibited[cons->inhibit_count] = ballot_id;
    cons->inhibit_until[cons->inhibit_count] = now + cons->config.inhibit_duration;
    cons->inhibit_count++;

    /* Mark local ballot as cancelled if we have it */
    int idx = find_ballot_index(cons, ballot_id);
    if (idx >= 0) {
        finalize_ballot(cons, &cons->ballots[idx], EKK_VOTE_CANCELLED);
    }

    /* Broadcast inhibit message */
    /* Note: Using vote message with INHIBIT value */
    send_vote(cons, EKK_BROADCAST_ID, ballot_id, EKK_VOTE_INHIBIT);

    return EKK_OK;
}

/* ============================================================================
 * INCOMING MESSAGE HANDLERS
 * ============================================================================ */

ekk_error_t ekk_consensus_on_vote(ekk_consensus_t *cons,
                                   ekk_module_id_t voter_id,
                                   ekk_ballot_id_t ballot_id,
                                   ekk_vote_value_t vote)
{
    if (cons == NULL || voter_id == EKK_INVALID_MODULE_ID ||
        ballot_id == EKK_INVALID_BALLOT_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Handle inhibition */
    if (vote == EKK_VOTE_INHIBIT) {
        ekk_time_us_t now = ekk_hal_time_us();

        /* Add to inhibit list */
        if (cons->inhibit_count < EKK_MAX_BALLOTS) {
            cons->inhibited[cons->inhibit_count] = ballot_id;
            cons->inhibit_until[cons->inhibit_count] = now + cons->config.inhibit_duration;
            cons->inhibit_count++;
        }

        /* Cancel local ballot */
        int idx = find_ballot_index(cons, ballot_id);
        if (idx >= 0 && !cons->ballots[idx].completed) {
            finalize_ballot(cons, &cons->ballots[idx], EKK_VOTE_CANCELLED);
        }

        return EKK_OK;
    }

    /* Find ballot */
    int idx = find_ballot_index(cons, ballot_id);
    if (idx < 0) {
        /* Unknown ballot - might be from a proposal we haven't seen */
        return EKK_ERR_NOT_FOUND;
    }

    ekk_ballot_t *ballot = &cons->ballots[idx];

    /* Only proposer can receive votes */
    if (ballot->proposer != cons->my_id) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Cannot vote on completed ballot */
    if (ballot->completed) {
        return EKK_OK;  /* Ignore late votes */
    }

    /* Find voter slot (use voter_id as index for simplicity) */
    uint32_t voter_slot = voter_id % EKK_K_NEIGHBORS;

    /* Check for duplicate vote */
    if (ballot->votes[voter_slot] != EKK_VOTE_ABSTAIN) {
        return EKK_OK;  /* Already voted */
    }

    /* Record vote */
    ballot->votes[voter_slot] = vote;
    ballot->vote_count++;

    switch (vote) {
        case EKK_VOTE_YES:
            ballot->yes_count++;
            break;
        case EKK_VOTE_NO:
            ballot->no_count++;
            break;
        default:
            break;
    }

    /* Check if we can determine result early */
    ekk_vote_result_t result = evaluate_ballot(ballot, EKK_K_NEIGHBORS);
    if (result != EKK_VOTE_PENDING) {
        finalize_ballot(cons, ballot, result);
    }

    return EKK_OK;
}

ekk_error_t ekk_consensus_on_proposal(ekk_consensus_t *cons,
                                       ekk_module_id_t proposer_id,
                                       ekk_ballot_id_t ballot_id,
                                       ekk_proposal_type_t type,
                                       uint32_t data,
                                       ekk_fixed_t threshold)
{
    if (cons == NULL || proposer_id == EKK_INVALID_MODULE_ID ||
        ballot_id == EKK_INVALID_BALLOT_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Ignore self proposals */
    if (proposer_id == cons->my_id) {
        return EKK_OK;
    }

    ekk_time_us_t now = ekk_hal_time_us();

    /* Check if inhibited */
    if (is_inhibited(cons, ballot_id, now)) {
        /* Send inhibit response */
        send_vote(cons, proposer_id, ballot_id, EKK_VOTE_INHIBIT);
        return EKK_ERR_INHIBITED;
    }

    /* Check if we already have this ballot */
    int idx = find_ballot_index(cons, ballot_id);
    if (idx >= 0) {
        /* Duplicate proposal */
        return EKK_OK;
    }

    /* Allocate slot for remote ballot tracking */
    idx = allocate_ballot_slot(cons);
    if (idx < 0) {
        /* No room - vote no */
        send_vote(cons, proposer_id, ballot_id, EKK_VOTE_NO);
        return EKK_ERR_BUSY;
    }

    /* Store ballot info */
    ekk_ballot_t *ballot = &cons->ballots[idx];
    memset(ballot, 0, sizeof(ekk_ballot_t));

    ballot->id = ballot_id;
    ballot->type = type;
    ballot->proposer = proposer_id;
    ballot->proposal_data = data;
    ballot->threshold = threshold;
    ballot->deadline = now + cons->config.vote_timeout;
    ballot->result = EKK_VOTE_PENDING;
    ballot->completed = false;

    cons->active_ballot_count++;

    /* Decide how to vote */
    ekk_vote_value_t my_vote = EKK_VOTE_ABSTAIN;

    if (g_decide_callback != NULL) {
        my_vote = g_decide_callback(cons, ballot);
    } else {
        /* Default: vote yes */
        my_vote = EKK_VOTE_YES;
    }

    /* Send vote */
    send_vote(cons, proposer_id, ballot_id, my_vote);

    return EKK_OK;
}

/* ============================================================================
 * QUERY
 * ============================================================================ */

ekk_vote_result_t ekk_consensus_get_result(const ekk_consensus_t *cons,
                                            ekk_ballot_id_t ballot_id)
{
    if (cons == NULL || ballot_id == EKK_INVALID_BALLOT_ID) {
        return EKK_VOTE_PENDING;
    }

    int idx = find_ballot_index(cons, ballot_id);
    if (idx < 0) {
        return EKK_VOTE_PENDING;  /* Unknown */
    }

    return cons->ballots[idx].result;
}

/* ============================================================================
 * PERIODIC TICK
 * ============================================================================ */

uint32_t ekk_consensus_tick(ekk_consensus_t *cons, ekk_time_us_t now)
{
    if (cons == NULL) {
        return 0;
    }

    uint32_t completed_count = 0;

    /* Check each active ballot for timeout */
    for (uint32_t i = 0; i < cons->active_ballot_count; i++) {
        ekk_ballot_t *ballot = &cons->ballots[i];

        if (ballot->completed) {
            continue;
        }

        /* Check for inhibition */
        if (is_inhibited(cons, ballot->id, now)) {
            finalize_ballot(cons, ballot, EKK_VOTE_CANCELLED);
            completed_count++;
            continue;
        }

        /* Check for timeout */
        if (now >= ballot->deadline) {
            /* Evaluate with votes received */
            ekk_vote_result_t result = evaluate_ballot(ballot, ballot->vote_count);

            if (result == EKK_VOTE_PENDING) {
                /* Not enough votes before timeout */
                result = EKK_VOTE_TIMEOUT;
            }

            finalize_ballot(cons, ballot, result);
            completed_count++;
        }
    }

    /* Clean up expired inhibitions */
    uint32_t write_idx = 0;
    for (uint32_t read_idx = 0; read_idx < cons->inhibit_count; read_idx++) {
        if (cons->inhibit_until[read_idx] > now) {
            if (write_idx != read_idx) {
                cons->inhibited[write_idx] = cons->inhibited[read_idx];
                cons->inhibit_until[write_idx] = cons->inhibit_until[read_idx];
            }
            write_idx++;
        }
    }
    cons->inhibit_count = write_idx;

    /* Clean up completed ballots periodically */
    if (completed_count > 0) {
        cleanup_completed_ballots(cons);
    }

    return completed_count;
}

/* ============================================================================
 * CALLBACKS
 * ============================================================================ */

void ekk_consensus_set_decide_callback(ekk_consensus_t *cons,
                                        ekk_consensus_decide_cb callback)
{
    EKK_UNUSED(cons);
    g_decide_callback = callback;
}

void ekk_consensus_set_complete_callback(ekk_consensus_t *cons,
                                          ekk_consensus_complete_cb callback)
{
    EKK_UNUSED(cons);
    g_complete_callback = callback;
}

/* ============================================================================
 * BYZANTINE QUARANTINE PROTOCOL (ROJ Paper Section V, Algorithm 3)
 * ============================================================================ */

/**
 * @brief Simple hash for evidence verification
 *
 * Computes a 32-bit hash of evidence data for quick verification.
 * Full evidence is verified separately when needed.
 */
static uint32_t compute_evidence_hash(const ekk_byzantine_evidence_t *evidence)
{
    uint32_t hash = 0x811c9dc5;  /* FNV-1a offset basis */
    const uint32_t prime = 0x01000193;

    /* Hash evidence type */
    hash ^= evidence->evidence_type;
    hash *= prime;

    /* Hash suspect ID */
    hash ^= evidence->suspect_id;
    hash *= prime;

    /* Hash witness count */
    hash ^= evidence->witness_count;
    hash *= prime;

    /* Hash evidence data */
    for (int i = 0; i < 24; i++) {
        hash ^= evidence->evidence_data[i];
        hash *= prime;
    }

    return hash;
}

ekk_error_t ekk_quarantine_init(ekk_quarantine_state_t *state)
{
    if (state == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    memset(state, 0, sizeof(ekk_quarantine_state_t));
    return EKK_OK;
}

ekk_error_t ekk_consensus_propose_quarantine(ekk_consensus_t *cons,
                                               ekk_module_id_t suspect_id,
                                               const ekk_byzantine_evidence_t *evidence,
                                               ekk_ballot_id_t *ballot_id)
{
    if (cons == NULL || suspect_id == EKK_INVALID_MODULE_ID ||
        evidence == NULL || ballot_id == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Verify evidence before proposing */
    if (!ekk_consensus_verify_evidence(evidence)) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Quarantine requires 2/3 supermajority */
    return ekk_consensus_propose(cons,
                                  EKK_PROPOSAL_QUARANTINE,
                                  (uint32_t)suspect_id,
                                  EKK_THRESHOLD_SUPERMAJORITY,
                                  ballot_id);
}

bool ekk_consensus_verify_evidence(const ekk_byzantine_evidence_t *evidence)
{
    if (evidence == NULL) {
        return false;
    }

    /* Verify evidence type is known */
    switch (evidence->evidence_type) {
        case EKK_EVIDENCE_EQUIVOCATION:
        case EKK_EVIDENCE_INVALID_MAC:
        case EKK_EVIDENCE_TIMING_ANOMALY:
        case EKK_EVIDENCE_STATE_INVALID:
            break;
        default:
            return false;
    }

    /* Verify suspect ID is valid */
    if (evidence->suspect_id == EKK_INVALID_MODULE_ID ||
        evidence->suspect_id >= EKK_MAX_MODULES) {
        return false;
    }

    /* Verify at least one witness */
    if (evidence->witness_count == 0) {
        return false;
    }

    /* Verify witness IDs are valid and not the suspect */
    for (uint8_t i = 0; i < evidence->witness_count && i < EKK_QUARANTINE_MAX_WITNESSES; i++) {
        ekk_module_id_t w = evidence->witness_ids[i];
        if (w == EKK_INVALID_MODULE_ID || w >= EKK_MAX_MODULES || w == evidence->suspect_id) {
            return false;
        }
    }

    /* Type-specific verification */
    switch (evidence->evidence_type) {
        case EKK_EVIDENCE_EQUIVOCATION:
            /* Evidence data should contain conflicting message hashes */
            /* First 4 bytes: term number */
            /* Next 8 bytes: hash of message 1 */
            /* Next 8 bytes: hash of message 2 */
            /* Messages should differ (non-zero XOR) */
            {
                uint64_t hash1, hash2;
                memcpy(&hash1, &evidence->evidence_data[4], 8);
                memcpy(&hash2, &evidence->evidence_data[12], 8);
                if (hash1 == hash2) {
                    return false;  /* Not actually equivocation */
                }
            }
            break;

        case EKK_EVIDENCE_INVALID_MAC:
            /* Evidence data should contain the invalid message + computed MAC */
            /* We can't verify this without the shared key, so accept if structured */
            break;

        case EKK_EVIDENCE_TIMING_ANOMALY:
            /* Evidence data: expected_time (8), actual_time (8), threshold (8) */
            {
                uint64_t expected, actual, threshold;
                memcpy(&expected, &evidence->evidence_data[0], 8);
                memcpy(&actual, &evidence->evidence_data[8], 8);
                memcpy(&threshold, &evidence->evidence_data[16], 8);

                /* Check that actual differs from expected by more than threshold */
                uint64_t diff = (actual > expected) ? (actual - expected) : (expected - actual);
                if (diff <= threshold) {
                    return false;  /* Within acceptable bounds */
                }
            }
            break;

        case EKK_EVIDENCE_STATE_INVALID:
            /* Evidence: from_state (1), to_state (1), valid_transitions bitmap (4) */
            {
                uint8_t from_state = evidence->evidence_data[0];
                uint8_t to_state = evidence->evidence_data[1];
                uint32_t valid_bitmap;
                memcpy(&valid_bitmap, &evidence->evidence_data[2], 4);

                /* Check if transition is in valid bitmap */
                uint32_t transition_bit = 1U << ((from_state * 8) + to_state);
                if (valid_bitmap & transition_bit) {
                    return false;  /* Transition was actually valid */
                }
            }
            break;
    }

    return true;
}

ekk_error_t ekk_consensus_execute_quarantine(ekk_consensus_t *cons,
                                               ekk_quarantine_state_t *state,
                                               ekk_module_id_t module_id,
                                               ekk_time_us_t now)
{
    if (cons == NULL || state == NULL ||
        module_id == EKK_INVALID_MODULE_ID || module_id >= EKK_MAX_MODULES) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Cannot quarantine self */
    if (module_id == cons->my_id) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Already quarantined? */
    if (state->quarantined[module_id]) {
        return EKK_OK;
    }

    /* Execute quarantine */
    state->quarantined[module_id] = true;
    state->quarantine_time[module_id] = now;
    state->quarantine_count++;

    return EKK_OK;
}

ekk_error_t ekk_quarantine_lift(ekk_quarantine_state_t *state,
                                  ekk_module_id_t module_id)
{
    if (state == NULL || module_id == EKK_INVALID_MODULE_ID ||
        module_id >= EKK_MAX_MODULES) {
        return EKK_ERR_INVALID_ARG;
    }

    if (!state->quarantined[module_id]) {
        return EKK_OK;  /* Not quarantined */
    }

    state->quarantined[module_id] = false;
    state->quarantine_time[module_id] = 0;
    state->quarantine_count--;

    return EKK_OK;
}
