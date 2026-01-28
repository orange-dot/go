/**
 * @file ekk_partition.c
 * @brief EK-KOR v2 - Network Partition Handling Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * ROJ Paper Section VI: Network Partition Resilience
 *
 * Implements the partition state machine for handling network splits:
 * - HEALTHY -> SUSPECTING -> MAJORITY/MINORITY -> RECONCILING -> HEALTHY
 * - Minority freeze mode prevents split-brain
 * - Epoch-based reconciliation for rejoining partitions
 * - Power ramping at 10%/sec during reintegration
 */

#include "ekk/ekk_partition.h"
#include "ekk/ekk_hal.h"

/* ============================================================================
 * STATE STRING CONVERSION
 * ============================================================================ */

const char* ekk_partition_state_str(ekk_partition_state_t state) {
    switch (state) {
        case EKK_PARTITION_HEALTHY:     return "HEALTHY";
        case EKK_PARTITION_SUSPECTING:  return "SUSPECTING";
        case EKK_PARTITION_MAJORITY:    return "MAJORITY";
        case EKK_PARTITION_MINORITY:    return "MINORITY";
        case EKK_PARTITION_RECONCILING: return "RECONCILING";
        default:                        return "UNKNOWN";
    }
}

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/**
 * @brief Transition to new state
 */
static void partition_transition(ekk_partition_ctx_t *ctx,
                                  ekk_partition_state_t new_state,
                                  ekk_time_us_t now) {
    if (ctx->state == new_state) {
        return;
    }

    ekk_partition_state_t old_state = ctx->state;
    ctx->state = new_state;
    ctx->state_enter_time = now;

    /* State entry actions */
    switch (new_state) {
        case EKK_PARTITION_HEALTHY:
            ctx->freeze_active = false;
            ctx->power_ramp_factor = EKK_FIXED_ONE;
            ctx->target_power_factor = EKK_FIXED_ONE;
            break;

        case EKK_PARTITION_SUSPECTING:
            ctx->suspecting_start = now;
            break;

        case EKK_PARTITION_MAJORITY:
            /* Increment epoch when becoming majority partition leader */
            ctx->epoch++;
            ctx->freeze_active = false;
            ctx->power_ramp_factor = EKK_FIXED_ONE;
            break;

        case EKK_PARTITION_MINORITY:
            /* Freeze mode: minimal power, no elections */
            ctx->freeze_active = true;
            ctx->power_ramp_factor = 0;
            break;

        case EKK_PARTITION_RECONCILING:
            /* Start power ramp from 0 */
            ctx->freeze_active = false;
            ctx->power_ramp_factor = 0;
            ctx->target_power_factor = EKK_FIXED_ONE;
            ctx->sync_received_count = 0;
            ctx->sync_complete = false;
            break;
    }

    EKK_UNUSED(old_state);
    /* Could log state transition here if needed */
}

/**
 * @brief Update power ramp during reconciliation
 *
 * Ramps at 10%/sec = 0.1/sec = 0.0001/ms = 0.0000001/us
 * Using Q16.16: 0.1 * 65536 = 6554 per second
 */
static void partition_update_power_ramp(ekk_partition_ctx_t *ctx,
                                         ekk_time_us_t elapsed_us) {
    if (ctx->power_ramp_factor >= ctx->target_power_factor) {
        ctx->power_ramp_factor = ctx->target_power_factor;
        return;
    }

    /* Calculate increment: rate * elapsed_time */
    /* rate = 0.1/sec = 6554 (Q16.16) per 1,000,000 us */
    /* increment = rate * elapsed_us / 1,000,000 */
    int64_t increment = ((int64_t)EKK_PARTITION_POWER_RAMP_RATE * elapsed_us) / 1000000;

    ctx->power_ramp_factor += (ekk_fixed_t)increment;

    if (ctx->power_ramp_factor > ctx->target_power_factor) {
        ctx->power_ramp_factor = ctx->target_power_factor;
    }
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================ */

ekk_error_t ekk_partition_init(ekk_partition_ctx_t *ctx, uint32_t total_modules) {
    if (ctx == NULL || total_modules == 0) {
        return EKK_ERR_INVALID_ARG;
    }

    ctx->state = EKK_PARTITION_HEALTHY;
    ctx->epoch = 1;  /* Start at epoch 1 */

    ctx->total_modules = total_modules;
    ctx->visible_count = total_modules;  /* Assume all visible initially */
    ctx->quorum_size = (total_modules / 2) + 1;  /* N/2 + 1 */

    ctx->state_enter_time = 0;
    ctx->last_tick = 0;
    ctx->suspecting_start = 0;

    ctx->power_ramp_factor = EKK_FIXED_ONE;
    ctx->target_power_factor = EKK_FIXED_ONE;

    ctx->freeze_active = false;

    ctx->sync_received_count = 0;
    ctx->sync_expected_count = 0;
    ctx->sync_complete = false;

    ctx->max_observed_epoch = 1;

    return EKK_OK;
}

ekk_error_t ekk_partition_tick(ekk_partition_ctx_t *ctx, ekk_time_us_t now) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    ekk_time_us_t elapsed = now - ctx->last_tick;
    ctx->last_tick = now;

    ekk_time_us_t time_in_state = now - ctx->state_enter_time;

    switch (ctx->state) {
        case EKK_PARTITION_HEALTHY:
            /* Check if we've lost quorum */
            if (ctx->visible_count < ctx->quorum_size) {
                partition_transition(ctx, EKK_PARTITION_SUSPECTING, now);
            }
            break;

        case EKK_PARTITION_SUSPECTING:
            /* Wait for confirmation timeout */
            if ((now - ctx->suspecting_start) >= EKK_PARTITION_CONFIRM_TIMEOUT_US) {
                if (ctx->visible_count >= ctx->quorum_size) {
                    /* Recovered - we have quorum */
                    partition_transition(ctx, EKK_PARTITION_MAJORITY, now);
                } else {
                    /* Confirmed minority */
                    partition_transition(ctx, EKK_PARTITION_MINORITY, now);
                }
            } else if (ctx->visible_count >= ctx->quorum_size) {
                /* Quick recovery - back to healthy if we recover before confirm */
                partition_transition(ctx, EKK_PARTITION_HEALTHY, now);
            }
            break;

        case EKK_PARTITION_MAJORITY:
            /* Check if all modules are back */
            if (ctx->visible_count >= ctx->total_modules) {
                partition_transition(ctx, EKK_PARTITION_HEALTHY, now);
            } else if (ctx->visible_count < ctx->quorum_size) {
                /* Lost majority - go back to suspecting */
                partition_transition(ctx, EKK_PARTITION_SUSPECTING, now);
            }
            break;

        case EKK_PARTITION_MINORITY:
            /* Waiting for reconciliation trigger (higher epoch observed) */
            if (ctx->max_observed_epoch > ctx->epoch) {
                /* Higher epoch module visible - start reconciliation */
                partition_transition(ctx, EKK_PARTITION_RECONCILING, now);
            }
            break;

        case EKK_PARTITION_RECONCILING:
            /* Update power ramp */
            partition_update_power_ramp(ctx, elapsed);

            /* Check if reconciliation complete */
            if (ctx->sync_complete && ctx->power_ramp_factor >= EKK_FIXED_ONE) {
                /* Adopt the higher epoch */
                if (ctx->max_observed_epoch > ctx->epoch) {
                    ctx->epoch = ctx->max_observed_epoch;
                }
                partition_transition(ctx, EKK_PARTITION_HEALTHY, now);
            }

            /* Timeout: force completion after max recovery time */
            if (time_in_state >= EKK_PARTITION_RECOVERY_MAX_US) {
                ctx->sync_complete = true;
                ctx->power_ramp_factor = EKK_FIXED_ONE;
                if (ctx->max_observed_epoch > ctx->epoch) {
                    ctx->epoch = ctx->max_observed_epoch;
                }
                partition_transition(ctx, EKK_PARTITION_HEALTHY, now);
            }
            break;
    }

    return EKK_OK;
}

ekk_error_t ekk_partition_on_visibility_change(ekk_partition_ctx_t *ctx,
                                                 uint32_t visible_count) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    ctx->visible_count = visible_count;

    /* Immediate state checks handled in tick() */

    return EKK_OK;
}

ekk_error_t ekk_partition_on_epoch_received(ekk_partition_ctx_t *ctx,
                                              uint32_t remote_epoch,
                                              ekk_module_id_t remote_id) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    EKK_UNUSED(remote_id);

    /* Track highest observed epoch */
    if (remote_epoch > ctx->max_observed_epoch) {
        ctx->max_observed_epoch = remote_epoch;
    }

    /* If we're in minority and see higher epoch, trigger reconciliation */
    if (ctx->state == EKK_PARTITION_MINORITY && remote_epoch > ctx->epoch) {
        /* Transition will happen in next tick */
    }

    return EKK_OK;
}

ekk_fixed_t ekk_partition_get_power_factor(const ekk_partition_ctx_t *ctx) {
    if (ctx == NULL) {
        return 0;
    }

    switch (ctx->state) {
        case EKK_PARTITION_HEALTHY:
        case EKK_PARTITION_MAJORITY:
            return EKK_FIXED_ONE;

        case EKK_PARTITION_SUSPECTING:
            /* Keep full power while suspecting */
            return EKK_FIXED_ONE;

        case EKK_PARTITION_MINORITY:
            /* Freeze mode: minimal power for droop response only */
            return 0;

        case EKK_PARTITION_RECONCILING:
            /* Ramping power */
            return ctx->power_ramp_factor;

        default:
            return 0;
    }
}

bool ekk_partition_can_vote(const ekk_partition_ctx_t *ctx) {
    if (ctx == NULL) {
        return false;
    }

    switch (ctx->state) {
        case EKK_PARTITION_HEALTHY:
        case EKK_PARTITION_MAJORITY:
        case EKK_PARTITION_SUSPECTING:
            return true;

        case EKK_PARTITION_MINORITY:
        case EKK_PARTITION_RECONCILING:
            /* No voting during minority or reconciliation */
            return false;

        default:
            return false;
    }
}

bool ekk_partition_can_elect(const ekk_partition_ctx_t *ctx) {
    if (ctx == NULL) {
        return false;
    }

    switch (ctx->state) {
        case EKK_PARTITION_HEALTHY:
        case EKK_PARTITION_MAJORITY:
            return true;

        case EKK_PARTITION_SUSPECTING:
            /* Don't start new elections while suspecting */
            return false;

        case EKK_PARTITION_MINORITY:
        case EKK_PARTITION_RECONCILING:
            /* Definitely no elections */
            return false;

        default:
            return false;
    }
}

ekk_error_t ekk_partition_on_sync_received(ekk_partition_ctx_t *ctx,
                                            ekk_module_id_t from_id) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    EKK_UNUSED(from_id);

    if (ctx->state == EKK_PARTITION_RECONCILING) {
        ctx->sync_received_count++;

        /* Simple completion: just need one sync to consider it complete
         * More sophisticated: could track specific modules */
        if (ctx->sync_received_count >= 1) {
            ctx->sync_complete = true;
        }
    }

    return EKK_OK;
}
