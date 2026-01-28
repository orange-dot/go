/**
 * @file ekk_spsc.h
 * @brief EK-KOR v2 - Single-Producer Single-Consumer Lock-Free Ring Buffer
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * SPSC (Single-Producer Single-Consumer) ring buffer for zero-copy IPC.
 *
 * Design goals:
 * - Lock-free: No mutexes or spinlocks
 * - Wait-free: Bounded operation time
 * - Zero-copy option: Can return pointer to slot for in-place access
 * - Cache-friendly: Head and tail on separate cache lines
 * - Target latency: < 100ns push/pop on Cortex-M4 @ 170MHz
 *
 * Use cases:
 * - CAN-FD message queues (ISR → task)
 * - Inter-module field updates
 * - Consensus vote collection
 */

#ifndef EKK_SPSC_H
#define EKK_SPSC_H

#include "ekk_types.h"
#include "ekk_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/**
 * @brief Default queue capacity (must be power of 2)
 */
#ifndef EKK_SPSC_DEFAULT_CAPACITY
#define EKK_SPSC_DEFAULT_CAPACITY   32
#endif

/**
 * @brief Cache line size for padding (Cortex-M4 has 32-byte lines)
 */
#ifndef EKK_CACHE_LINE_SIZE
#define EKK_CACHE_LINE_SIZE         32
#endif

/* ============================================================================
 * SPSC QUEUE STRUCTURE
 * ============================================================================ */

/**
 * @brief SPSC ring buffer control structure
 *
 * Head and tail are on separate cache lines to avoid false sharing
 * between producer (writes head) and consumer (writes tail).
 */
typedef struct {
    /* Producer side - only producer writes head */
    volatile uint32_t head;             /**< Next write index */
    uint8_t _pad_head[EKK_CACHE_LINE_SIZE - sizeof(uint32_t)];

    /* Consumer side - only consumer writes tail */
    volatile uint32_t tail;             /**< Next read index */
    uint8_t _pad_tail[EKK_CACHE_LINE_SIZE - sizeof(uint32_t)];

    /* Shared (read-only after init) */
    void *buffer;                       /**< Pre-allocated buffer */
    uint32_t capacity;                  /**< Number of slots (power of 2) */
    uint32_t mask;                      /**< capacity - 1, for fast modulo */
    uint32_t item_size;                 /**< Size of each item in bytes */
} ekk_spsc_t;

/* ============================================================================
 * SPSC API
 * ============================================================================ */

/**
 * @brief Initialize SPSC queue with pre-allocated buffer
 *
 * @param q Queue structure to initialize
 * @param buffer Pre-allocated buffer for items (must be capacity * item_size bytes)
 * @param capacity Number of slots (MUST be power of 2)
 * @param item_size Size of each item in bytes
 * @return EKK_OK on success, EKK_ERR_INVALID_ARG if capacity not power of 2
 *
 * @note Buffer memory must remain valid for lifetime of queue
 * @note For zero-copy usage, buffer should be cache-aligned
 */
ekk_error_t ekk_spsc_init(ekk_spsc_t *q, void *buffer,
                           uint32_t capacity, uint32_t item_size);

/**
 * @brief Reset queue to empty state
 *
 * @warning Only safe to call when no concurrent access
 */
void ekk_spsc_reset(ekk_spsc_t *q);

/* ============================================================================
 * PRODUCER API (Call from single producer thread/ISR only)
 * ============================================================================ */

/**
 * @brief Push item to queue (copy semantics)
 *
 * Copies item_size bytes from item into next available slot.
 *
 * @param q Queue
 * @param item Pointer to item to push
 * @return EKK_OK on success, EKK_ERR_NO_MEMORY if queue full
 *
 * @note Thread-safe with respect to consumer (lock-free)
 * @note NOT safe to call from multiple producers
 */
ekk_error_t ekk_spsc_push(ekk_spsc_t *q, const void *item);

/**
 * @brief Get pointer to next write slot (zero-copy push)
 *
 * Returns pointer to next available slot for in-place construction.
 * Caller must call ekk_spsc_push_commit() after writing to slot.
 *
 * @param q Queue
 * @return Pointer to slot, or NULL if queue full
 *
 * @note Slot is NOT visible to consumer until commit
 * @example
 *   can_frame_t *slot = ekk_spsc_push_acquire(&q);
 *   if (slot) {
 *       slot->id = 0x123;
 *       slot->len = 8;
 *       memcpy(slot->data, rx_data, 8);
 *       ekk_spsc_push_commit(&q);
 *   }
 */
void *ekk_spsc_push_acquire(ekk_spsc_t *q);

/**
 * @brief Commit previously acquired push slot
 *
 * Makes the slot visible to consumer.
 *
 * @param q Queue
 */
void ekk_spsc_push_commit(ekk_spsc_t *q);

/* ============================================================================
 * CONSUMER API (Call from single consumer thread only)
 * ============================================================================ */

/**
 * @brief Pop item from queue (copy semantics)
 *
 * Copies item_size bytes from oldest slot into item.
 *
 * @param q Queue
 * @param[out] item Pointer to receive item
 * @return EKK_OK on success, EKK_ERR_NOT_FOUND if queue empty
 *
 * @note Thread-safe with respect to producer (lock-free)
 * @note NOT safe to call from multiple consumers
 */
ekk_error_t ekk_spsc_pop(ekk_spsc_t *q, void *item);

/**
 * @brief Peek at oldest item without removing (zero-copy read)
 *
 * Returns pointer to oldest item for in-place access.
 * Caller must call ekk_spsc_pop_release() after reading.
 *
 * @param q Queue
 * @return Pointer to item, or NULL if queue empty
 *
 * @note Item remains in queue until release
 * @example
 *   can_frame_t *frame = ekk_spsc_pop_peek(&q);
 *   if (frame) {
 *       process_frame(frame);
 *       ekk_spsc_pop_release(&q);
 *   }
 */
void *ekk_spsc_pop_peek(ekk_spsc_t *q);

/**
 * @brief Release previously peeked slot
 *
 * Removes the slot from queue, allowing producer to reuse it.
 *
 * @param q Queue
 */
void ekk_spsc_pop_release(ekk_spsc_t *q);

/* ============================================================================
 * QUERY API (Safe from any thread)
 * ============================================================================ */

/**
 * @brief Get number of items in queue
 *
 * @note Value may be stale by time it's used (informational only)
 */
static inline uint32_t ekk_spsc_len(const ekk_spsc_t *q) {
    uint32_t head = q->head;
    uint32_t tail = q->tail;
    return (head - tail) & q->mask;
}

/**
 * @brief Check if queue is empty
 */
static inline bool ekk_spsc_is_empty(const ekk_spsc_t *q) {
    return q->head == q->tail;
}

/**
 * @brief Check if queue is full
 */
static inline bool ekk_spsc_is_full(const ekk_spsc_t *q) {
    return ((q->head + 1) & q->mask) == q->tail;
}

/**
 * @brief Get available space in queue
 */
static inline uint32_t ekk_spsc_available(const ekk_spsc_t *q) {
    return q->capacity - 1 - ekk_spsc_len(q);
}

/* ============================================================================
 * TYPED QUEUE MACROS
 * ============================================================================ */

/**
 * @brief Declare a statically-sized typed SPSC queue
 *
 * @example
 *   EKK_SPSC_DECLARE(can_tx_queue, can_frame_t, 32);
 *
 *   void init(void) {
 *       EKK_SPSC_INIT(can_tx_queue, can_frame_t, 32);
 *   }
 */
#define EKK_SPSC_DECLARE(name, type, cap) \
    static type name##_buffer[cap]; \
    static ekk_spsc_t name

#define EKK_SPSC_INIT(name, type, cap) \
    ekk_spsc_init(&name, name##_buffer, cap, sizeof(type))

/**
 * @brief Type-safe push macro
 */
#define EKK_SPSC_PUSH(q, item_ptr) \
    ekk_spsc_push(&(q), (item_ptr))

/**
 * @brief Type-safe pop macro
 */
#define EKK_SPSC_POP(q, item_ptr) \
    ekk_spsc_pop(&(q), (item_ptr))

#ifdef __cplusplus
}
#endif

#endif /* EKK_SPSC_H */
