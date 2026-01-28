/**
 * @file ekk_spsc.c
 * @brief EK-KOR v2 - SPSC Ring Buffer Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#include "ekk/ekk_spsc.h"
#include <string.h>

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

/**
 * @brief Check if value is power of 2
 */
static inline bool is_power_of_2(uint32_t n) {
    return (n != 0) && ((n & (n - 1)) == 0);
}

/**
 * @brief Get pointer to slot at index
 */
static inline void *slot_ptr(const ekk_spsc_t *q, uint32_t index) {
    return (uint8_t *)q->buffer + (index * q->item_size);
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

ekk_error_t ekk_spsc_init(ekk_spsc_t *q, void *buffer,
                           uint32_t capacity, uint32_t item_size) {
    if (!q || !buffer || capacity == 0 || item_size == 0) {
        return EKK_ERR_INVALID_ARG;
    }

    if (!is_power_of_2(capacity)) {
        return EKK_ERR_INVALID_ARG;
    }

    q->buffer = buffer;
    q->capacity = capacity;
    q->mask = capacity - 1;
    q->item_size = item_size;
    q->head = 0;
    q->tail = 0;

    /* Clear buffer */
    memset(buffer, 0, capacity * item_size);

    return EKK_OK;
}

void ekk_spsc_reset(ekk_spsc_t *q) {
    q->head = 0;
    q->tail = 0;
}

/* ============================================================================
 * PRODUCER API
 * ============================================================================ */

ekk_error_t ekk_spsc_push(ekk_spsc_t *q, const void *item) {
    uint32_t head = q->head;
    uint32_t next_head = (head + 1) & q->mask;

    /* Check if full */
    if (next_head == q->tail) {
        return EKK_ERR_NO_MEMORY;
    }

    /* Copy item to slot */
    memcpy(slot_ptr(q, head), item, q->item_size);

    /* Memory barrier ensures item is written before head update */
    ekk_hal_memory_barrier();

    /* Update head (makes item visible to consumer) */
    q->head = next_head;

    return EKK_OK;
}

void *ekk_spsc_push_acquire(ekk_spsc_t *q) {
    uint32_t head = q->head;
    uint32_t next_head = (head + 1) & q->mask;

    /* Check if full */
    if (next_head == q->tail) {
        return NULL;
    }

    return slot_ptr(q, head);
}

void ekk_spsc_push_commit(ekk_spsc_t *q) {
    /* Memory barrier ensures item is written before head update */
    ekk_hal_memory_barrier();

    /* Update head */
    q->head = (q->head + 1) & q->mask;
}

/* ============================================================================
 * CONSUMER API
 * ============================================================================ */

ekk_error_t ekk_spsc_pop(ekk_spsc_t *q, void *item) {
    uint32_t tail = q->tail;

    /* Check if empty */
    if (tail == q->head) {
        return EKK_ERR_NOT_FOUND;
    }

    /* Memory barrier ensures we see latest head */
    ekk_hal_memory_barrier();

    /* Copy item from slot */
    memcpy(item, slot_ptr(q, tail), q->item_size);

    /* Memory barrier ensures item is read before tail update */
    ekk_hal_memory_barrier();

    /* Update tail (frees slot for producer) */
    q->tail = (tail + 1) & q->mask;

    return EKK_OK;
}

void *ekk_spsc_pop_peek(ekk_spsc_t *q) {
    uint32_t tail = q->tail;

    /* Check if empty */
    if (tail == q->head) {
        return NULL;
    }

    /* Memory barrier ensures we see latest head */
    ekk_hal_memory_barrier();

    return slot_ptr(q, tail);
}

void ekk_spsc_pop_release(ekk_spsc_t *q) {
    /* Memory barrier ensures item is processed before tail update */
    ekk_hal_memory_barrier();

    /* Update tail */
    q->tail = (q->tail + 1) & q->mask;
}
