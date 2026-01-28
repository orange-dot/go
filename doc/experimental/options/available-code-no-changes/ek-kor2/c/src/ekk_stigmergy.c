/**
 * @file ekk_stigmergy.c
 * @brief EK-KOR v2 - Stigmergy Thermal Optimization Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * ROJ Paper Section VII, Algorithm 4: Stigmergy-based Thermal Balancing
 *
 * Implements ant-inspired stigmergy for thermal load balancing:
 * - Heat tags with exponential decay: tag(t) = tag(t-1) * exp(-dt/tau)
 * - Temperature-driven tag updates: hot modules increase tag, cool decrease
 * - Rank-based load redistribution: cooler modules take more load
 *
 * Target metric: 88% temperature variance reduction in 60 minutes
 */

#include "ekk/ekk_field.h"
#include "ekk/ekk_hal.h"

/* ============================================================================
 * EXPONENTIAL DECAY APPROXIMATION
 * ============================================================================ */

/**
 * @brief Lookup table for exp(-x) where x = [0, 4] in steps of 0.25
 *
 * Values in Q16.16 format.
 * exp(-0.00) = 1.000000 = 65536
 * exp(-0.25) = 0.778801 = 51042
 * exp(-0.50) = 0.606531 = 39755
 * ...
 * exp(-4.00) = 0.018316 = 1200
 */
static const ekk_fixed_t exp_lut[17] = {
    65536,  /* exp(-0.00) = 1.0 */
    51042,  /* exp(-0.25) */
    39755,  /* exp(-0.50) */
    30965,  /* exp(-0.75) */
    24109,  /* exp(-1.00) */
    18775,  /* exp(-1.25) */
    14622,  /* exp(-1.50) */
    11387,  /* exp(-1.75) */
    8869,   /* exp(-2.00) */
    6906,   /* exp(-2.25) */
    5378,   /* exp(-2.50) */
    4188,   /* exp(-2.75) */
    3261,   /* exp(-3.00) */
    2540,   /* exp(-3.25) */
    1978,   /* exp(-3.50) */
    1540,   /* exp(-3.75) */
    1200,   /* exp(-4.00) */
};

/**
 * @brief Compute exp(-elapsed/tau) using lookup + linear interpolation
 *
 * For x = elapsed/tau:
 * - If x >= 4.0, return ~0 (exponential essentially zero)
 * - Otherwise, linear interpolate between lookup table entries
 */
ekk_fixed_t ekk_fixed_exp_decay(ekk_time_us_t elapsed_us, ekk_time_us_t tau_us) {
    if (tau_us == 0) {
        return 0;
    }

    /* Compute x = elapsed / tau in Q16.16 */
    /* To avoid overflow, scale appropriately */
    uint64_t x_scaled = ((uint64_t)elapsed_us << 16) / tau_us;

    /* Clamp to [0, 4.0] range (4.0 in Q16.16 = 262144) */
    if (x_scaled >= 262144) {
        return 0;  /* Essentially zero */
    }

    /* Convert to table index: x / 0.25 = x * 4 */
    /* Index range: [0, 16] for x in [0, 4] */
    uint32_t x_fixed = (uint32_t)x_scaled;
    uint32_t index = (x_fixed * 4) >> 16;  /* Divide by 0.25 in fixed-point */

    if (index >= 16) {
        return exp_lut[16];
    }

    /* Linear interpolation between lut[index] and lut[index+1] */
    ekk_fixed_t y0 = exp_lut[index];
    ekk_fixed_t y1 = exp_lut[index + 1];

    /* Fractional part for interpolation */
    /* frac = (x - index * 0.25) / 0.25 = (x * 4 - index) in Q16.16 terms */
    uint32_t frac = ((x_fixed * 4) & 0xFFFF);  /* Fractional part */

    /* result = y0 + (y1 - y0) * frac */
    int32_t delta = y1 - y0;
    ekk_fixed_t interp = y0 + (ekk_fixed_t)((delta * frac) >> 16);

    return interp;
}

/* ============================================================================
 * STIGMERGY IMPLEMENTATION
 * ============================================================================ */

ekk_error_t ekk_stigmergy_init(ekk_stigmergy_ctx_t *ctx,
                                const ekk_stigmergy_config_t *config) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Apply default config if none provided */
    if (config != NULL) {
        ctx->config = *config;
    } else {
        ekk_stigmergy_config_t default_config = EKK_STIGMERGY_CONFIG_DEFAULT;
        ctx->config = default_config;
    }

    /* Initialize state */
    ctx->my_tag = 0;
    ctx->neighbor_count = 0;
    ctx->last_update = 0;

    for (int i = 0; i < EKK_K_NEIGHBORS; i++) {
        ctx->neighbor_tags[i] = 0;
        ctx->neighbor_ids[i] = EKK_INVALID_MODULE_ID;
    }

    return EKK_OK;
}

ekk_error_t ekk_stigmergy_update_tag(ekk_stigmergy_ctx_t *ctx,
                                       ekk_fixed_t Tj,
                                       ekk_time_us_t now) {
    if (ctx == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Compute elapsed time since last update */
    ekk_time_us_t elapsed = 0;
    if (ctx->last_update > 0 && now > ctx->last_update) {
        elapsed = now - ctx->last_update;
    }
    ctx->last_update = now;

    /* Step 1: Apply exponential decay */
    if (elapsed > 0) {
        ekk_fixed_t decay_factor = ekk_fixed_exp_decay(elapsed, ctx->config.tau);
        ctx->my_tag = EKK_FIXED_MUL(ctx->my_tag, decay_factor);
    }

    /* Step 2: Update based on temperature delta */
    ekk_fixed_t delta_T = Tj - ctx->config.target_temp;

    if (delta_T > ctx->config.temp_threshold) {
        /* Hot: increase tag */
        ekk_fixed_t adjustment = EKK_FIXED_MUL(ctx->config.alpha, delta_T);
        ctx->my_tag += adjustment;
    } else if (delta_T < -ctx->config.temp_threshold) {
        /* Cool: decrease tag */
        ekk_fixed_t adjustment = EKK_FIXED_MUL(ctx->config.alpha, -delta_T);
        ctx->my_tag -= adjustment;

        /* Clamp to non-negative */
        if (ctx->my_tag < 0) {
            ctx->my_tag = 0;
        }
    }
    /* Else: within deadband, no change */

    return EKK_OK;
}

ekk_error_t ekk_stigmergy_update_neighbor(ekk_stigmergy_ctx_t *ctx,
                                            ekk_module_id_t neighbor_id,
                                            ekk_fixed_t neighbor_tag) {
    if (ctx == NULL || neighbor_id == EKK_INVALID_MODULE_ID) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Find existing entry or empty slot */
    int slot = -1;
    for (int i = 0; i < EKK_K_NEIGHBORS; i++) {
        if (ctx->neighbor_ids[i] == neighbor_id) {
            /* Update existing */
            ctx->neighbor_tags[i] = neighbor_tag;
            return EKK_OK;
        }
        if (slot < 0 && ctx->neighbor_ids[i] == EKK_INVALID_MODULE_ID) {
            slot = i;
        }
    }

    /* Add to empty slot if available */
    if (slot >= 0) {
        ctx->neighbor_ids[slot] = neighbor_id;
        ctx->neighbor_tags[slot] = neighbor_tag;
        ctx->neighbor_count++;
        return EKK_OK;
    }

    /* No room - could implement LRU replacement here */
    return EKK_ERR_NO_MEMORY;
}

/**
 * @brief Compute rank of my_tag among all tags (0 = lowest/coolest)
 */
static int compute_rank(ekk_fixed_t my_tag,
                         const ekk_fixed_t *neighbor_tags,
                         int count) {
    int rank = 0;

    for (int i = 0; i < count; i++) {
        if (neighbor_tags[i] < my_tag) {
            rank++;
        }
    }

    return rank;
}

ekk_fixed_t ekk_stigmergy_compute_load_factor(const ekk_stigmergy_ctx_t *ctx) {
    if (ctx == NULL || ctx->neighbor_count == 0) {
        return 0;  /* No adjustment if no neighbors */
    }

    /* Compute rank among neighbors */
    int rank = compute_rank(ctx->my_tag, ctx->neighbor_tags, ctx->neighbor_count);

    /* Total nodes = neighbors + self */
    int total = ctx->neighbor_count + 1;

    /* Compute adjustment:
     * adjustment = max_adjust * (total/2 - rank) / total
     *
     * - If rank = 0 (coolest): adjustment = max_adjust * (total/2) / total ≈ +max_adjust/2
     * - If rank = total-1 (hottest): adjustment = max_adjust * (total/2 - total + 1) / total ≈ -max_adjust/2
     *
     * This gives a range of approximately [-max_adjust/2, +max_adjust/2]
     */
    int center = total / 2;
    int deviation = center - rank;

    /* deviation / total in Q16.16 */
    ekk_fixed_t ratio = EKK_FIXED_DIV(deviation << 16, total << 16);

    /* Multiply by max_load_adjust */
    ekk_fixed_t adjustment = EKK_FIXED_MUL(ctx->config.max_load_adjust, ratio);

    return adjustment;
}
