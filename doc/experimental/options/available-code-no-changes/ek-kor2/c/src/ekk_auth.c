/**
 * @file ekk_auth.c
 * @brief EK-KOR v2 - Chaskey MAC Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements Chaskey-12 MAC algorithm for lightweight authentication.
 *
 * Reference: Mouha et al. (2014) "Chaskey: An Efficient MAC Algorithm
 * for 32-bit Microcontrollers" - IACR ePrint 2014/386
 */

#include "ekk/ekk_auth.h"
#include "ekk/ekk_hal.h"
#include <string.h>

/* ============================================================================
 * CHASKEY PERMUTATION
 * ============================================================================ */

/**
 * @brief Rotate left 32-bit value
 */
static inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

/**
 * @brief Single Chaskey round
 *
 * Operates on 128-bit state v[0..3]
 */
static inline void chaskey_round(uint32_t v[4]) {
    v[0] += v[1];
    v[1] = rotl32(v[1], 5);
    v[1] ^= v[0];
    v[0] = rotl32(v[0], 16);

    v[2] += v[3];
    v[3] = rotl32(v[3], 8);
    v[3] ^= v[2];

    v[0] += v[3];
    v[3] = rotl32(v[3], 13);
    v[3] ^= v[0];

    v[2] += v[1];
    v[1] = rotl32(v[1], 7);
    v[1] ^= v[2];
    v[2] = rotl32(v[2], 16);
}

/**
 * @brief Full Chaskey permutation (EKK_CHASKEY_ROUNDS rounds)
 */
static void chaskey_permute(uint32_t v[4]) {
    for (int i = 0; i < EKK_CHASKEY_ROUNDS; i++) {
        chaskey_round(v);
    }
}

/* ============================================================================
 * SUBKEY DERIVATION
 * ============================================================================ */

/**
 * @brief Multiply by x in GF(2^128) with reduction polynomial x^128 + x^7 + x^2 + x + 1
 *
 * Used for subkey derivation: K1 = 2*K, K2 = 4*K
 */
static void times_two(uint32_t out[4], const uint32_t in[4]) {
    /* Check if MSB is set (will require reduction) */
    uint32_t msb = (in[3] >> 31) & 1;

    /* Shift left by 1 bit across all words */
    out[3] = (in[3] << 1) | (in[2] >> 31);
    out[2] = (in[2] << 1) | (in[1] >> 31);
    out[1] = (in[1] << 1) | (in[0] >> 31);
    out[0] = (in[0] << 1);

    /* If MSB was set, XOR with reduction constant */
    /* x^128 + x^7 + x^2 + x + 1 → 0x87 in little-endian representation */
    out[0] ^= (0x87 & (-(int32_t)msb));
}

/* ============================================================================
 * KEY MANAGEMENT
 * ============================================================================ */

void ekk_auth_key_init(ekk_auth_key_t *key, const uint8_t raw_key[16]) {
    /* Copy master key (little-endian words) */
    memcpy(key->k, raw_key, 16);

    /* Derive K1 = 2 * K */
    times_two(key->k1, key->k);

    /* Derive K2 = 4 * K = 2 * K1 */
    times_two(key->k2, key->k1);
}

void ekk_auth_key_clear(ekk_auth_key_t *key) {
    /* Secure wipe - write zeros and barrier to prevent optimization */
    volatile uint8_t *p = (volatile uint8_t *)key;
    for (size_t i = 0; i < sizeof(ekk_auth_key_t); i++) {
        p[i] = 0;
    }
    ekk_hal_memory_barrier();
}

/* ============================================================================
 * ONE-SHOT MAC API
 * ============================================================================ */

void ekk_auth_compute(const ekk_auth_key_t *key,
                       const void *message, uint32_t len,
                       ekk_auth_tag_t *tag) {
    const uint8_t *msg = (const uint8_t *)message;
    uint32_t v[4];

    /* Initialize state with key */
    v[0] = key->k[0];
    v[1] = key->k[1];
    v[2] = key->k[2];
    v[3] = key->k[3];

    /* Process complete 16-byte blocks */
    while (len > 16) {
        /* XOR message block into state */
        v[0] ^= ((uint32_t)msg[0]) | ((uint32_t)msg[1] << 8) |
                ((uint32_t)msg[2] << 16) | ((uint32_t)msg[3] << 24);
        v[1] ^= ((uint32_t)msg[4]) | ((uint32_t)msg[5] << 8) |
                ((uint32_t)msg[6] << 16) | ((uint32_t)msg[7] << 24);
        v[2] ^= ((uint32_t)msg[8]) | ((uint32_t)msg[9] << 8) |
                ((uint32_t)msg[10] << 16) | ((uint32_t)msg[11] << 24);
        v[3] ^= ((uint32_t)msg[12]) | ((uint32_t)msg[13] << 8) |
                ((uint32_t)msg[14] << 16) | ((uint32_t)msg[15] << 24);

        /* Permute */
        chaskey_permute(v);

        msg += 16;
        len -= 16;
    }

    /* Process final block (with padding if needed) */
    uint8_t last_block[16] = {0};
    memcpy(last_block, msg, len);

    if (len < 16) {
        /* Incomplete block: pad with 0x01 || 0x00* and use K2 */
        last_block[len] = 0x01;

        v[0] ^= key->k2[0];
        v[1] ^= key->k2[1];
        v[2] ^= key->k2[2];
        v[3] ^= key->k2[3];
    } else {
        /* Complete block: use K1 */
        v[0] ^= key->k1[0];
        v[1] ^= key->k1[1];
        v[2] ^= key->k1[2];
        v[3] ^= key->k1[3];
    }

    /* XOR final block */
    v[0] ^= ((uint32_t)last_block[0]) | ((uint32_t)last_block[1] << 8) |
            ((uint32_t)last_block[2] << 16) | ((uint32_t)last_block[3] << 24);
    v[1] ^= ((uint32_t)last_block[4]) | ((uint32_t)last_block[5] << 8) |
            ((uint32_t)last_block[6] << 16) | ((uint32_t)last_block[7] << 24);
    v[2] ^= ((uint32_t)last_block[8]) | ((uint32_t)last_block[9] << 8) |
            ((uint32_t)last_block[10] << 16) | ((uint32_t)last_block[11] << 24);
    v[3] ^= ((uint32_t)last_block[12]) | ((uint32_t)last_block[13] << 8) |
            ((uint32_t)last_block[14] << 16) | ((uint32_t)last_block[15] << 24);

    /* Final permutation */
    chaskey_permute(v);

    /* XOR with key again */
    v[0] ^= key->k[0];
    v[1] ^= key->k[1];
    v[2] ^= key->k[2];
    v[3] ^= key->k[3];

    /* Output tag (truncated to EKK_MAC_TAG_SIZE) */
    uint8_t full_tag[16];
    full_tag[0] = v[0] & 0xFF;
    full_tag[1] = (v[0] >> 8) & 0xFF;
    full_tag[2] = (v[0] >> 16) & 0xFF;
    full_tag[3] = (v[0] >> 24) & 0xFF;
    full_tag[4] = v[1] & 0xFF;
    full_tag[5] = (v[1] >> 8) & 0xFF;
    full_tag[6] = (v[1] >> 16) & 0xFF;
    full_tag[7] = (v[1] >> 24) & 0xFF;
    full_tag[8] = v[2] & 0xFF;
    full_tag[9] = (v[2] >> 8) & 0xFF;
    full_tag[10] = (v[2] >> 16) & 0xFF;
    full_tag[11] = (v[2] >> 24) & 0xFF;
    full_tag[12] = v[3] & 0xFF;
    full_tag[13] = (v[3] >> 8) & 0xFF;
    full_tag[14] = (v[3] >> 16) & 0xFF;
    full_tag[15] = (v[3] >> 24) & 0xFF;

    memcpy(tag->bytes, full_tag, EKK_MAC_TAG_SIZE);
}

/**
 * @brief Constant-time comparison
 */
static bool constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

bool ekk_auth_verify(const ekk_auth_key_t *key,
                      const void *message, uint32_t len,
                      const ekk_auth_tag_t *tag) {
    ekk_auth_tag_t computed;
    ekk_auth_compute(key, message, len, &computed);
    return constant_time_compare(computed.bytes, tag->bytes, EKK_MAC_TAG_SIZE);
}

/* ============================================================================
 * INCREMENTAL MAC API
 * ============================================================================ */

void ekk_auth_init(ekk_auth_ctx_t *ctx, const ekk_auth_key_t *key) {
    ctx->key = key;
    ctx->v[0] = key->k[0];
    ctx->v[1] = key->k[1];
    ctx->v[2] = key->k[2];
    ctx->v[3] = key->k[3];
    ctx->buflen = 0;
    ctx->msglen = 0;
}

void ekk_auth_update(ekk_auth_ctx_t *ctx, const void *data, uint32_t len) {
    const uint8_t *msg = (const uint8_t *)data;
    ctx->msglen += len;

    /* If we have buffered data, try to complete a block */
    if (ctx->buflen > 0) {
        uint32_t need = 16 - ctx->buflen;
        if (len < need) {
            memcpy(ctx->buffer + ctx->buflen, msg, len);
            ctx->buflen += len;
            return;
        }

        memcpy(ctx->buffer + ctx->buflen, msg, need);
        msg += need;
        len -= need;

        /* Process buffered block */
        ctx->v[0] ^= ((uint32_t)ctx->buffer[0]) | ((uint32_t)ctx->buffer[1] << 8) |
                     ((uint32_t)ctx->buffer[2] << 16) | ((uint32_t)ctx->buffer[3] << 24);
        ctx->v[1] ^= ((uint32_t)ctx->buffer[4]) | ((uint32_t)ctx->buffer[5] << 8) |
                     ((uint32_t)ctx->buffer[6] << 16) | ((uint32_t)ctx->buffer[7] << 24);
        ctx->v[2] ^= ((uint32_t)ctx->buffer[8]) | ((uint32_t)ctx->buffer[9] << 8) |
                     ((uint32_t)ctx->buffer[10] << 16) | ((uint32_t)ctx->buffer[11] << 24);
        ctx->v[3] ^= ((uint32_t)ctx->buffer[12]) | ((uint32_t)ctx->buffer[13] << 8) |
                     ((uint32_t)ctx->buffer[14] << 16) | ((uint32_t)ctx->buffer[15] << 24);
        chaskey_permute(ctx->v);
        ctx->buflen = 0;
    }

    /* Process complete blocks */
    while (len > 16) {
        ctx->v[0] ^= ((uint32_t)msg[0]) | ((uint32_t)msg[1] << 8) |
                     ((uint32_t)msg[2] << 16) | ((uint32_t)msg[3] << 24);
        ctx->v[1] ^= ((uint32_t)msg[4]) | ((uint32_t)msg[5] << 8) |
                     ((uint32_t)msg[6] << 16) | ((uint32_t)msg[7] << 24);
        ctx->v[2] ^= ((uint32_t)msg[8]) | ((uint32_t)msg[9] << 8) |
                     ((uint32_t)msg[10] << 16) | ((uint32_t)msg[11] << 24);
        ctx->v[3] ^= ((uint32_t)msg[12]) | ((uint32_t)msg[13] << 8) |
                     ((uint32_t)msg[14] << 16) | ((uint32_t)msg[15] << 24);
        chaskey_permute(ctx->v);
        msg += 16;
        len -= 16;
    }

    /* Buffer remaining data */
    if (len > 0) {
        memcpy(ctx->buffer, msg, len);
        ctx->buflen = len;
    }
}

void ekk_auth_final(ekk_auth_ctx_t *ctx, ekk_auth_tag_t *tag) {
    /* Prepare final block with padding */
    uint8_t last_block[16] = {0};
    memcpy(last_block, ctx->buffer, ctx->buflen);

    if (ctx->buflen < 16) {
        last_block[ctx->buflen] = 0x01;
        ctx->v[0] ^= ctx->key->k2[0];
        ctx->v[1] ^= ctx->key->k2[1];
        ctx->v[2] ^= ctx->key->k2[2];
        ctx->v[3] ^= ctx->key->k2[3];
    } else {
        ctx->v[0] ^= ctx->key->k1[0];
        ctx->v[1] ^= ctx->key->k1[1];
        ctx->v[2] ^= ctx->key->k1[2];
        ctx->v[3] ^= ctx->key->k1[3];
    }

    ctx->v[0] ^= ((uint32_t)last_block[0]) | ((uint32_t)last_block[1] << 8) |
                 ((uint32_t)last_block[2] << 16) | ((uint32_t)last_block[3] << 24);
    ctx->v[1] ^= ((uint32_t)last_block[4]) | ((uint32_t)last_block[5] << 8) |
                 ((uint32_t)last_block[6] << 16) | ((uint32_t)last_block[7] << 24);
    ctx->v[2] ^= ((uint32_t)last_block[8]) | ((uint32_t)last_block[9] << 8) |
                 ((uint32_t)last_block[10] << 16) | ((uint32_t)last_block[11] << 24);
    ctx->v[3] ^= ((uint32_t)last_block[12]) | ((uint32_t)last_block[13] << 8) |
                 ((uint32_t)last_block[14] << 16) | ((uint32_t)last_block[15] << 24);

    chaskey_permute(ctx->v);

    ctx->v[0] ^= ctx->key->k[0];
    ctx->v[1] ^= ctx->key->k[1];
    ctx->v[2] ^= ctx->key->k[2];
    ctx->v[3] ^= ctx->key->k[3];

    /* Output tag */
    uint8_t full_tag[16];
    full_tag[0] = ctx->v[0] & 0xFF;
    full_tag[1] = (ctx->v[0] >> 8) & 0xFF;
    full_tag[2] = (ctx->v[0] >> 16) & 0xFF;
    full_tag[3] = (ctx->v[0] >> 24) & 0xFF;
    full_tag[4] = ctx->v[1] & 0xFF;
    full_tag[5] = (ctx->v[1] >> 8) & 0xFF;
    full_tag[6] = (ctx->v[1] >> 16) & 0xFF;
    full_tag[7] = (ctx->v[1] >> 24) & 0xFF;
    full_tag[8] = ctx->v[2] & 0xFF;
    full_tag[9] = (ctx->v[2] >> 8) & 0xFF;
    full_tag[10] = (ctx->v[2] >> 16) & 0xFF;
    full_tag[11] = (ctx->v[2] >> 24) & 0xFF;
    full_tag[12] = ctx->v[3] & 0xFF;
    full_tag[13] = (ctx->v[3] >> 8) & 0xFF;
    full_tag[14] = (ctx->v[3] >> 16) & 0xFF;
    full_tag[15] = (ctx->v[3] >> 24) & 0xFF;

    memcpy(tag->bytes, full_tag, EKK_MAC_TAG_SIZE);
}

/* ============================================================================
 * MESSAGE AUTHENTICATION HELPERS
 * ============================================================================ */

bool ekk_auth_is_required(uint8_t msg_type) {
    switch (msg_type) {
        case 0x04: /* EKK_MSG_PROPOSAL */
            return EKK_AUTH_REQUIRED_PROPOSAL != 0;
        case 0x05: /* EKK_MSG_VOTE */
            return EKK_AUTH_REQUIRED_VOTE != 0;
        case 0x08: /* EKK_MSG_SHUTDOWN (emergency) */
            return EKK_AUTH_REQUIRED_EMERGENCY != 0;
        case 0x01: /* EKK_MSG_HEARTBEAT */
            return EKK_AUTH_REQUIRED_HEARTBEAT != 0;
        case 0x02: /* EKK_MSG_DISCOVERY */
            return EKK_AUTH_REQUIRED_DISCOVERY != 0;
        default:
            return false;
    }
}

void ekk_auth_message(const ekk_auth_key_t *key,
                       uint8_t sender_id, uint8_t msg_type,
                       const void *data, uint32_t len,
                       ekk_auth_tag_t *tag) {
    ekk_auth_ctx_t ctx;
    ekk_auth_init(&ctx, key);

    /* Authenticate: sender_id | msg_type | data */
    uint8_t header[2] = { sender_id, msg_type };
    ekk_auth_update(&ctx, header, 2);
    if (data && len > 0) {
        ekk_auth_update(&ctx, data, len);
    }

    ekk_auth_final(&ctx, tag);
}

bool ekk_auth_verify_message(const ekk_auth_key_t *key,
                              uint8_t sender_id, uint8_t msg_type,
                              const void *data, uint32_t len,
                              const ekk_auth_tag_t *tag) {
    ekk_auth_tag_t computed;
    ekk_auth_message(key, sender_id, msg_type, data, len, &computed);
    return constant_time_compare(computed.bytes, tag->bytes, EKK_MAC_TAG_SIZE);
}

/* ============================================================================
 * KEYRING MANAGEMENT
 * ============================================================================ */

void ekk_auth_keyring_init(ekk_auth_keyring_t *ring) {
    memset(ring, 0, sizeof(ekk_auth_keyring_t));
}

void ekk_auth_keyring_set(ekk_auth_keyring_t *ring,
                           ekk_module_id_t id,
                           const uint8_t raw_key[16]) {
    if (id == 0 || id > EKK_MAX_MODULES) return;

    ekk_auth_key_init(&ring->keys[id], raw_key);
    ring->valid[id / 8] |= (1 << (id % 8));
}

const ekk_auth_key_t *ekk_auth_keyring_get(const ekk_auth_keyring_t *ring,
                                            ekk_module_id_t id) {
    if (!ekk_auth_keyring_has(ring, id)) {
        return NULL;
    }
    return &ring->keys[id];
}

bool ekk_auth_keyring_has(const ekk_auth_keyring_t *ring, ekk_module_id_t id) {
    if (id == 0 || id > EKK_MAX_MODULES) return false;
    return (ring->valid[id / 8] & (1 << (id % 8))) != 0;
}

void ekk_auth_keyring_clear(ekk_auth_keyring_t *ring, ekk_module_id_t id) {
    if (id == 0 || id > EKK_MAX_MODULES) return;

    ekk_auth_key_clear(&ring->keys[id]);
    ring->valid[id / 8] &= ~(1 << (id % 8));
}
