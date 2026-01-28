/**
 * @file msg_queue.c
 * @brief Per-Core Message Queues for Raspberry Pi 3B+ HAL
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements lock-free SPSC (single-producer single-consumer) message
 * queues for inter-core communication. Each core has its own receive
 * queue, following the x86 HAL pattern.
 *
 * Design:
 * - Each core has a dedicated receive queue
 * - Queues are cache-line aligned to avoid false sharing
 * - Lock-free using memory barriers (no spinlocks needed for SPSC)
 * - Messages are small (64 bytes max) to fit in cache lines
 */

#include "rpi3_hw.h"
#include "msg_queue.h"
#include "smp.h"
#include <string.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MSG_QUEUE_SIZE      64      /* Messages per queue */
#define MSG_MAX_LEN         64      /* Max payload size */
#define MAX_CORES           4

/* Cache line size for padding */
#define CACHE_LINE_SIZE     64

/* ============================================================================
 * Message Structure
 * ============================================================================ */

typedef struct {
    uint8_t sender_id;              /* Sender's module ID */
    uint8_t msg_type;               /* Message type */
    uint8_t len;                    /* Payload length */
    uint8_t _reserved;
    uint8_t data[MSG_MAX_LEN];      /* Payload */
    volatile uint32_t valid;        /* 1 if message is valid (write-release) */
} __attribute__((aligned(8))) msg_slot_t;

/* Ensure message fits in reasonable space */
_Static_assert(sizeof(msg_slot_t) <= 80, "msg_slot_t too large");

/**
 * @brief Per-core message queue
 *
 * Aligned to cache line to avoid false sharing between cores.
 */
typedef struct {
    msg_slot_t slots[MSG_QUEUE_SIZE];
    volatile uint32_t head;         /* Write index (producer only) */
    volatile uint32_t tail;         /* Read index (consumer only) */
    uint32_t _pad[14];              /* Pad to multiple of cache line */
} __attribute__((aligned(CACHE_LINE_SIZE))) core_queue_t;

/* ============================================================================
 * External Symbols
 * ============================================================================ */

/* Message queue region from linker script */
extern char __msg_queues[];

/* ============================================================================
 * Private Variables
 * ============================================================================ */

/* Pointer to per-core queue array */
static core_queue_t *g_queues = NULL;

/* Receive callback (optional) */
static msg_recv_callback_t g_recv_callback = NULL;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize message queues (call from each core)
 */
void msg_queue_init(void)
{
    uint32_t core_id = smp_get_core_id();

    /* Set queue pointer (same for all cores) */
    g_queues = (core_queue_t *)__msg_queues;

    /* Only core 0 clears all queues */
    if (core_id == 0) {
        memset(g_queues, 0, sizeof(core_queue_t) * MAX_CORES);
    }

    /* Memory barrier to ensure initialization is visible */
    __asm__ volatile("dmb sy" ::: "memory");
}

/* ============================================================================
 * Send Functions
 * ============================================================================ */

/**
 * @brief Send message to a specific core
 *
 * @param dest_core Destination core (0-3) or MSG_BROADCAST for all
 * @param sender_id Sender's module ID
 * @param msg_type Message type
 * @param data Payload
 * @param len Payload length
 * @return 0 on success, -1 on error (queue full)
 */
int msg_queue_send(uint32_t dest_core, uint8_t sender_id,
                   uint8_t msg_type, const void *data, uint32_t len)
{
    /* Handle broadcast */
    if (dest_core == MSG_BROADCAST) {
        uint32_t my_core = smp_get_core_id();
        int result = 0;

        for (uint32_t i = 0; i < MAX_CORES; i++) {
            if (i != my_core) {
                if (msg_queue_send(i, sender_id, msg_type, data, len) < 0) {
                    result = -1;  /* At least one failed */
                }
            }
        }
        return result;
    }

    /* Validate parameters */
    if (dest_core >= MAX_CORES || len > MSG_MAX_LEN) {
        return -1;
    }

    core_queue_t *q = &g_queues[dest_core];
    uint32_t head = q->head;
    uint32_t next_head = (head + 1) % MSG_QUEUE_SIZE;

    /* Check if queue is full */
    if (next_head == q->tail) {
        return -1;  /* Queue full */
    }

    /* Write message */
    msg_slot_t *slot = &q->slots[head];
    slot->sender_id = sender_id;
    slot->msg_type = msg_type;
    slot->len = (uint8_t)len;

    if (data && len > 0) {
        memcpy(slot->data, data, len);
    }

    /* Memory barrier before setting valid flag */
    __asm__ volatile("dmb sy" ::: "memory");

    slot->valid = 1;

    /* Memory barrier after setting valid flag */
    __asm__ volatile("dmb sy" ::: "memory");

    /* Advance head */
    q->head = next_head;

    return 0;
}

/* ============================================================================
 * Receive Functions
 * ============================================================================ */

/**
 * @brief Receive message from this core's queue
 *
 * @param[out] sender_id Sender's module ID
 * @param[out] msg_type Message type
 * @param[out] data Buffer for payload
 * @param[in,out] len Max length in, actual length out
 * @return 0 on success, -1 if no message
 */
int msg_queue_recv(uint8_t *sender_id, uint8_t *msg_type,
                   void *data, uint32_t *len)
{
    uint32_t core_id = smp_get_core_id();
    core_queue_t *q = &g_queues[core_id];

    uint32_t tail = q->tail;

    /* Check if queue is empty */
    if (tail == q->head) {
        return -1;  /* No message */
    }

    msg_slot_t *slot = &q->slots[tail];

    /* Memory barrier before reading */
    __asm__ volatile("dmb sy" ::: "memory");

    /* Check valid flag */
    if (!slot->valid) {
        return -1;  /* Message not ready yet */
    }

    /* Read message */
    if (sender_id) *sender_id = slot->sender_id;
    if (msg_type) *msg_type = slot->msg_type;

    uint32_t msg_len = slot->len;
    if (len) {
        if (*len < msg_len) {
            msg_len = *len;  /* Truncate if buffer too small */
        }
        *len = msg_len;
    }

    if (data && msg_len > 0) {
        memcpy(data, slot->data, msg_len);
    }

    /* Memory barrier before clearing */
    __asm__ volatile("dmb sy" ::: "memory");

    /* Clear valid flag */
    slot->valid = 0;

    /* Memory barrier after clearing */
    __asm__ volatile("dmb sy" ::: "memory");

    /* Advance tail */
    q->tail = (tail + 1) % MSG_QUEUE_SIZE;

    return 0;
}

/**
 * @brief Check if this core's queue has messages
 * @return Non-zero if messages available
 */
int msg_queue_has_message(void)
{
    uint32_t core_id = smp_get_core_id();
    core_queue_t *q = &g_queues[core_id];
    return q->tail != q->head;
}

/**
 * @brief Get number of messages in this core's queue
 * @return Number of messages
 */
uint32_t msg_queue_count(void)
{
    uint32_t core_id = smp_get_core_id();
    core_queue_t *q = &g_queues[core_id];

    uint32_t head = q->head;
    uint32_t tail = q->tail;

    if (head >= tail) {
        return head - tail;
    } else {
        return MSG_QUEUE_SIZE - tail + head;
    }
}

/* ============================================================================
 * Callback Support
 * ============================================================================ */

/**
 * @brief Set receive callback
 *
 * The callback is invoked when msg_queue_poll() finds a message.
 *
 * @param callback Function to call on message receive
 */
void msg_queue_set_callback(msg_recv_callback_t callback)
{
    g_recv_callback = callback;
}

/**
 * @brief Poll for messages and invoke callback
 *
 * Call this from main loop to process incoming messages.
 *
 * @return Number of messages processed
 */
uint32_t msg_queue_poll(void)
{
    uint32_t count = 0;
    uint8_t sender_id, msg_type;
    uint8_t data[MSG_MAX_LEN];
    uint32_t len;

    while (1) {
        len = MSG_MAX_LEN;
        if (msg_queue_recv(&sender_id, &msg_type, data, &len) < 0) {
            break;
        }

        count++;

        if (g_recv_callback) {
            g_recv_callback(sender_id, msg_type, data, len);
        }
    }

    return count;
}
