/**
 * @file ekk_auth.h
 * @brief EK-KOR v2 - Chaskey MAC Lightweight Authentication
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements Chaskey MAC for lightweight message authentication.
 *
 * Chaskey is a 128-bit block cipher-based MAC designed for microcontrollers:
 * - 128-bit key, 128-bit tag (truncatable to 64-bit)
 * - 12 rounds (Chaskey-12) for standard security
 * - 8 rounds (Chaskey-LTS) for long-term security
 * - ~1-2μs per 16-byte block on Cortex-M4 @ 170MHz
 *
 * Reference:
 * - Mouha et al. (2014): "Chaskey: An Efficient MAC Algorithm for 32-bit Microcontrollers"
 * - IACR ePrint 2014/386
 *
 * Use cases:
 * - Vote message authentication (prevent Byzantine attacks)
 * - Emergency shutdown command verification
 * - Proposal authenticity
 */

#ifndef EKK_AUTH_H
#define EKK_AUTH_H

#include "ekk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/**
 * @brief Chaskey variant selection
 *
 * CHASKEY_12: 12 rounds, sufficient for most applications
 * CHASKEY_LTS: 8 rounds of double-round structure (16 permutation rounds)
 */
#ifndef EKK_CHASKEY_ROUNDS
#define EKK_CHASKEY_ROUNDS          12
#endif

/**
 * @brief MAC tag size in bytes (8 = 64-bit truncated, 16 = full 128-bit)
 */
#ifndef EKK_MAC_TAG_SIZE
#define EKK_MAC_TAG_SIZE            8
#endif

/**
 * @brief Message types requiring authentication
 *
 * Set to 1 to require MAC for this message type, 0 to skip.
 */
#ifndef EKK_AUTH_REQUIRED_VOTE
#define EKK_AUTH_REQUIRED_VOTE          1
#endif

#ifndef EKK_AUTH_REQUIRED_PROPOSAL
#define EKK_AUTH_REQUIRED_PROPOSAL      1
#endif

#ifndef EKK_AUTH_REQUIRED_EMERGENCY
#define EKK_AUTH_REQUIRED_EMERGENCY     1
#endif

#ifndef EKK_AUTH_REQUIRED_HEARTBEAT
#define EKK_AUTH_REQUIRED_HEARTBEAT     0   /**< Optional: high overhead */
#endif

#ifndef EKK_AUTH_REQUIRED_DISCOVERY
#define EKK_AUTH_REQUIRED_DISCOVERY     0   /**< Optional: initial trust */
#endif

/* ============================================================================
 * KEY STRUCTURE
 * ============================================================================ */

/**
 * @brief Chaskey key structure
 *
 * Contains master key and two derived subkeys (K1, K2) for
 * message finalization (per Chaskey specification).
 */
typedef struct {
    uint32_t k[4];      /**< Master key (128 bits) */
    uint32_t k1[4];     /**< Subkey 1 (for complete blocks) */
    uint32_t k2[4];     /**< Subkey 2 (for incomplete final block) */
} ekk_auth_key_t;

/**
 * @brief MAC context for incremental computation
 */
typedef struct {
    uint32_t v[4];      /**< Current state */
    uint8_t buffer[16]; /**< Partial block buffer */
    uint32_t buflen;    /**< Bytes in buffer */
    uint32_t msglen;    /**< Total message length */
    const ekk_auth_key_t *key;  /**< Key reference */
} ekk_auth_ctx_t;

/**
 * @brief MAC tag (output)
 */
typedef struct {
    uint8_t bytes[EKK_MAC_TAG_SIZE];
} ekk_auth_tag_t;

/* ============================================================================
 * KEY MANAGEMENT
 * ============================================================================ */

/**
 * @brief Initialize key structure from raw 128-bit key
 *
 * Computes subkeys K1 and K2 from master key.
 *
 * @param key Key structure to initialize
 * @param raw_key 16 bytes of key material
 */
void ekk_auth_key_init(ekk_auth_key_t *key, const uint8_t raw_key[16]);

/**
 * @brief Clear key material from memory
 *
 * Securely wipes key structure to prevent leakage.
 *
 * @param key Key to clear
 */
void ekk_auth_key_clear(ekk_auth_key_t *key);

/* ============================================================================
 * ONE-SHOT MAC API
 * ============================================================================ */

/**
 * @brief Compute MAC tag for message (one-shot)
 *
 * @param key Initialized key
 * @param message Message bytes
 * @param len Message length
 * @param[out] tag Output MAC tag
 */
void ekk_auth_compute(const ekk_auth_key_t *key,
                       const void *message, uint32_t len,
                       ekk_auth_tag_t *tag);

/**
 * @brief Verify MAC tag for message
 *
 * @param key Initialized key
 * @param message Message bytes
 * @param len Message length
 * @param tag Expected MAC tag
 * @return true if tag matches, false otherwise
 *
 * @note Uses constant-time comparison to prevent timing attacks
 */
bool ekk_auth_verify(const ekk_auth_key_t *key,
                      const void *message, uint32_t len,
                      const ekk_auth_tag_t *tag);

/* ============================================================================
 * INCREMENTAL MAC API
 * ============================================================================ */

/**
 * @brief Initialize MAC context for incremental computation
 *
 * @param ctx Context to initialize
 * @param key Key to use
 */
void ekk_auth_init(ekk_auth_ctx_t *ctx, const ekk_auth_key_t *key);

/**
 * @brief Update MAC with additional data
 *
 * Can be called multiple times to process message in chunks.
 *
 * @param ctx MAC context
 * @param data Data chunk
 * @param len Chunk length
 */
void ekk_auth_update(ekk_auth_ctx_t *ctx, const void *data, uint32_t len);

/**
 * @brief Finalize MAC computation and output tag
 *
 * @param ctx MAC context
 * @param[out] tag Output MAC tag
 *
 * @note Context is invalid after this call; must reinit to reuse
 */
void ekk_auth_final(ekk_auth_ctx_t *ctx, ekk_auth_tag_t *tag);

/* ============================================================================
 * AUTHENTICATED MESSAGE HELPERS
 * ============================================================================ */

/**
 * @brief Check if message type requires authentication
 *
 * @param msg_type EKK_MSG_* type from ekk_hal.h
 * @return true if MAC required, false otherwise
 */
bool ekk_auth_is_required(uint8_t msg_type);

/**
 * @brief Compute MAC for EK-KOR message
 *
 * Authenticates: sender_id | msg_type | data
 *
 * @param key Sender's key
 * @param sender_id Sender module ID
 * @param msg_type Message type
 * @param data Message payload
 * @param len Payload length
 * @param[out] tag Output MAC tag
 */
void ekk_auth_message(const ekk_auth_key_t *key,
                       uint8_t sender_id, uint8_t msg_type,
                       const void *data, uint32_t len,
                       ekk_auth_tag_t *tag);

/**
 * @brief Verify MAC for received EK-KOR message
 *
 * @param key Sender's key (must have sender's key)
 * @param sender_id Claimed sender module ID
 * @param msg_type Message type
 * @param data Message payload
 * @param len Payload length
 * @param tag Received MAC tag
 * @return true if authentic, false if forged/corrupted
 */
bool ekk_auth_verify_message(const ekk_auth_key_t *key,
                              uint8_t sender_id, uint8_t msg_type,
                              const void *data, uint32_t len,
                              const ekk_auth_tag_t *tag);

/* ============================================================================
 * KEY DISTRIBUTION SUPPORT
 * ============================================================================ */

/**
 * @brief Key slot for per-module keys
 *
 * In a cluster, each module has a shared secret with each neighbor.
 * This structure holds keys indexed by module ID.
 */
typedef struct {
    ekk_auth_key_t keys[EKK_MAX_MODULES];   /**< Key per module */
    uint8_t valid[(EKK_MAX_MODULES + 7) / 8]; /**< Bitmap: 1 = key present */
} ekk_auth_keyring_t;

/**
 * @brief Initialize keyring (all keys invalid)
 */
void ekk_auth_keyring_init(ekk_auth_keyring_t *ring);

/**
 * @brief Set key for module
 */
void ekk_auth_keyring_set(ekk_auth_keyring_t *ring,
                           ekk_module_id_t id,
                           const uint8_t raw_key[16]);

/**
 * @brief Get key for module
 *
 * @return Pointer to key, or NULL if no key for this module
 */
const ekk_auth_key_t *ekk_auth_keyring_get(const ekk_auth_keyring_t *ring,
                                            ekk_module_id_t id);

/**
 * @brief Check if key exists for module
 */
bool ekk_auth_keyring_has(const ekk_auth_keyring_t *ring, ekk_module_id_t id);

/**
 * @brief Clear key for module
 */
void ekk_auth_keyring_clear(ekk_auth_keyring_t *ring, ekk_module_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* EKK_AUTH_H */
