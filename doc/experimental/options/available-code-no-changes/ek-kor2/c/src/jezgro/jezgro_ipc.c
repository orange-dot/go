/**
 * @file jezgro_ipc.c
 * @brief JEZGRO Microkernel - IPC Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license AGPL-3.0
 */

#include "../../include/ekk/jezgro/jezgro_ipc.h"
#include "../../include/ekk/ekk_hal.h"
#include <string.h>

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

/** Endpoint states */
static jezgro_ipc_endpoint_state_t g_endpoints[JEZGRO_IPC_MAX_ENDPOINTS];

/** Current service context (set by scheduler) */
static uint8_t g_current_service = 0;

/** Notification callback */
static jezgro_ipc_notify_cb g_notify_callback = NULL;

/** IRQ to endpoint mapping */
static jezgro_endpoint_t g_irq_endpoints[64];

/** Statistics */
static jezgro_ipc_stats_t g_stats;

/** IPC initialized */
static bool g_ipc_initialized = false;

/* ============================================================================
 * QUEUE OPERATIONS
 * ============================================================================ */

static void queue_init(jezgro_ipc_queue_t* q)
{
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

static bool queue_full(const jezgro_ipc_queue_t* q)
{
    return q->count >= JEZGRO_IPC_QUEUE_DEPTH;
}

static bool queue_empty(const jezgro_ipc_queue_t* q)
{
    return q->count == 0;
}

static jezgro_ipc_error_t queue_push(jezgro_ipc_queue_t* q,
                                      const jezgro_ipc_msg_t* msg)
{
    if (queue_full(q)) {
        g_stats.queue_overflows++;
        return JEZGRO_IPC_ERR_FULL;
    }

    memcpy(&q->messages[q->head], msg, sizeof(jezgro_ipc_msg_t));
    q->head = (q->head + 1) % JEZGRO_IPC_QUEUE_DEPTH;
    q->count++;

    return JEZGRO_IPC_OK;
}

static jezgro_ipc_error_t queue_pop(jezgro_ipc_queue_t* q,
                                     jezgro_ipc_msg_t* msg)
{
    if (queue_empty(q)) {
        return JEZGRO_IPC_ERR_EMPTY;
    }

    memcpy(msg, &q->messages[q->tail], sizeof(jezgro_ipc_msg_t));
    q->tail = (q->tail + 1) % JEZGRO_IPC_QUEUE_DEPTH;
    q->count--;

    return JEZGRO_IPC_OK;
}

/* ============================================================================
 * ENDPOINT LOOKUP
 * ============================================================================ */

static jezgro_ipc_endpoint_state_t* find_endpoint(jezgro_endpoint_t ep)
{
    for (int i = 0; i < JEZGRO_IPC_MAX_ENDPOINTS; i++) {
        if (g_endpoints[i].active && g_endpoints[i].id == ep) {
            return &g_endpoints[i];
        }
    }
    return NULL;
}

static jezgro_ipc_endpoint_state_t* current_endpoint(void)
{
    for (int i = 0; i < JEZGRO_IPC_MAX_ENDPOINTS; i++) {
        if (g_endpoints[i].active &&
            g_endpoints[i].owner_service == g_current_service) {
            return &g_endpoints[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

ekk_error_t jezgro_ipc_init(void)
{
    if (g_ipc_initialized) {
        return EKK_OK;
    }

    /* Clear endpoint states */
    memset(g_endpoints, 0, sizeof(g_endpoints));

    /* Clear IRQ mapping */
    memset(g_irq_endpoints, JEZGRO_EP_KERNEL, sizeof(g_irq_endpoints));

    /* Clear statistics */
    memset(&g_stats, 0, sizeof(g_stats));

    /* Register kernel endpoint */
    jezgro_ipc_register_endpoint(JEZGRO_EP_KERNEL, 0);

    g_ipc_initialized = true;

    ekk_hal_printf("JEZGRO: IPC initialized\n");

    return EKK_OK;
}

/* ============================================================================
 * ENDPOINT MANAGEMENT
 * ============================================================================ */

ekk_error_t jezgro_ipc_register_endpoint(jezgro_endpoint_t endpoint,
                                          uint8_t service_id)
{
    /* Check for duplicate */
    if (find_endpoint(endpoint) != NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Find free slot */
    jezgro_ipc_endpoint_state_t* ep = NULL;
    for (int i = 0; i < JEZGRO_IPC_MAX_ENDPOINTS; i++) {
        if (!g_endpoints[i].active) {
            ep = &g_endpoints[i];
            break;
        }
    }

    if (ep == NULL) {
        return EKK_ERR_LIMIT;
    }

    ep->id = endpoint;
    ep->owner_service = service_id;
    ep->active = true;
    ep->seq_counter = 0;
    ep->waiting = false;
    queue_init(&ep->rx_queue);

    ekk_hal_printf("JEZGRO: IPC endpoint %d registered for service %d\n",
                   endpoint, service_id);

    return EKK_OK;
}

ekk_error_t jezgro_ipc_unregister_endpoint(jezgro_endpoint_t endpoint)
{
    jezgro_ipc_endpoint_state_t* ep = find_endpoint(endpoint);
    if (ep == NULL) {
        return EKK_ERR_NOT_FOUND;
    }

    ep->active = false;
    return EKK_OK;
}

bool jezgro_ipc_endpoint_exists(jezgro_endpoint_t endpoint)
{
    return find_endpoint(endpoint) != NULL;
}

/* ============================================================================
 * MESSAGE SENDING
 * ============================================================================ */

static jezgro_ipc_error_t internal_send(jezgro_endpoint_t src,
                                         jezgro_endpoint_t dst,
                                         jezgro_ipc_msg_type_t type,
                                         uint16_t seq,
                                         const void* payload,
                                         uint16_t len)
{
    /* Handle broadcast */
    if (dst == JEZGRO_EP_BROADCAST) {
        for (int i = 0; i < JEZGRO_IPC_MAX_ENDPOINTS; i++) {
            if (g_endpoints[i].active && g_endpoints[i].id != src) {
                internal_send(src, g_endpoints[i].id, type, seq, payload, len);
            }
        }
        return JEZGRO_IPC_OK;
    }

    /* Find destination */
    jezgro_ipc_endpoint_state_t* dst_ep = find_endpoint(dst);
    if (dst_ep == NULL) {
        g_stats.dead_service_errors++;
        return JEZGRO_IPC_ERR_DEAD;
    }

    /* Build message */
    jezgro_ipc_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.src = src;
    msg.header.dst = dst;
    msg.header.type = type;
    msg.header.seq = seq;
    msg.header.len = len;

    if (payload != NULL && len > 0) {
        uint16_t copy_len = len;
        if (copy_len > sizeof(msg.payload)) {
            copy_len = sizeof(msg.payload);
        }
        memcpy(msg.payload, payload, copy_len);
    }

    /* Enqueue */
    jezgro_ipc_error_t err = queue_push(&dst_ep->rx_queue, &msg);
    if (err != JEZGRO_IPC_OK) {
        return err;
    }

    g_stats.messages_sent++;

    /* Wake up receiver if waiting */
    if (dst_ep->waiting) {
        dst_ep->waiting = false;
        /* In a full implementation, this would signal a semaphore
         * or trigger a context switch */
    }

    return JEZGRO_IPC_OK;
}

jezgro_ipc_error_t jezgro_ipc_send(jezgro_endpoint_t dst,
                                    jezgro_ipc_msg_type_t type,
                                    const void* payload,
                                    uint16_t len)
{
    jezgro_ipc_endpoint_state_t* src_ep = current_endpoint();
    if (src_ep == NULL) {
        return JEZGRO_IPC_ERR_INVALID;
    }

    uint16_t seq = src_ep->seq_counter++;

    return internal_send(src_ep->id, dst, type, seq, payload, len);
}

/* ============================================================================
 * MESSAGE RECEIVING
 * ============================================================================ */

jezgro_ipc_error_t jezgro_ipc_recv(jezgro_ipc_msg_t* msg,
                                    uint32_t timeout_ms)
{
    jezgro_ipc_endpoint_state_t* ep = current_endpoint();
    if (ep == NULL) {
        return JEZGRO_IPC_ERR_INVALID;
    }

    uint32_t start_time = ekk_hal_time_ms();
    uint32_t deadline = (timeout_ms == JEZGRO_IPC_WAIT_FOREVER) ?
                        0xFFFFFFFF : start_time + timeout_ms;

    while (1) {
        /* Try to receive */
        jezgro_ipc_error_t err = queue_pop(&ep->rx_queue, msg);
        if (err == JEZGRO_IPC_OK) {
            g_stats.messages_received++;

            /* Handle notification callback */
            if (msg->header.type == JEZGRO_IPC_MSG_NOTIFY && g_notify_callback) {
                g_notify_callback(msg->header.src,
                                  msg->payload,
                                  msg->header.len);
            }

            return JEZGRO_IPC_OK;
        }

        /* Check timeout */
        uint32_t now = ekk_hal_time_ms();
        if (timeout_ms != JEZGRO_IPC_WAIT_FOREVER && now >= deadline) {
            g_stats.timeouts++;
            return JEZGRO_IPC_ERR_TIMEOUT;
        }

        /* Mark as waiting and yield */
        ep->waiting = true;
        ep->wait_deadline = deadline;

        /* In a full implementation, this would yield to scheduler.
         * For now, just busy-wait with a small delay. */
        ekk_hal_delay_us(100);
    }
}

jezgro_ipc_error_t jezgro_ipc_recv_nonblock(jezgro_ipc_msg_t* msg)
{
    jezgro_ipc_endpoint_state_t* ep = current_endpoint();
    if (ep == NULL) {
        return JEZGRO_IPC_ERR_INVALID;
    }

    jezgro_ipc_error_t err = queue_pop(&ep->rx_queue, msg);
    if (err == JEZGRO_IPC_OK) {
        g_stats.messages_received++;
    }

    return err;
}

/* ============================================================================
 * REQUEST/REPLY PATTERN
 * ============================================================================ */

jezgro_ipc_error_t jezgro_ipc_call(jezgro_endpoint_t dst,
                                    const void* request,
                                    uint16_t request_len,
                                    void* reply,
                                    uint16_t* reply_len,
                                    uint32_t timeout_ms)
{
    jezgro_ipc_endpoint_state_t* src_ep = current_endpoint();
    if (src_ep == NULL) {
        return JEZGRO_IPC_ERR_INVALID;
    }

    uint16_t seq = src_ep->seq_counter++;

    /* Send request */
    jezgro_ipc_error_t err = internal_send(src_ep->id, dst,
                                            JEZGRO_IPC_MSG_REQUEST,
                                            seq, request, request_len);
    if (err != JEZGRO_IPC_OK) {
        return err;
    }

    /* Wait for reply with matching seq */
    uint32_t start_time = ekk_hal_time_ms();
    uint32_t deadline = (timeout_ms == JEZGRO_IPC_WAIT_FOREVER) ?
                        0xFFFFFFFF : start_time + timeout_ms;

    while (1) {
        jezgro_ipc_msg_t msg;
        err = jezgro_ipc_recv(&msg, timeout_ms);

        if (err == JEZGRO_IPC_ERR_TIMEOUT) {
            return err;
        }

        if (err == JEZGRO_IPC_OK &&
            msg.header.type == JEZGRO_IPC_MSG_REPLY &&
            msg.header.src == dst &&
            msg.header.seq == seq) {

            /* Got our reply */
            uint16_t copy_len = msg.header.len;
            if (copy_len > *reply_len) {
                copy_len = *reply_len;
            }

            if (reply != NULL && copy_len > 0) {
                memcpy(reply, msg.payload, copy_len);
            }
            *reply_len = msg.header.len;

            return JEZGRO_IPC_OK;
        }

        /* Got a different message - requeue and continue waiting */
        /* Note: This is a simplified implementation. A proper one
         * would handle out-of-order messages better. */

        uint32_t now = ekk_hal_time_ms();
        if (timeout_ms != JEZGRO_IPC_WAIT_FOREVER && now >= deadline) {
            g_stats.timeouts++;
            return JEZGRO_IPC_ERR_TIMEOUT;
        }

        timeout_ms = deadline - now;
    }
}

jezgro_ipc_error_t jezgro_ipc_reply(const jezgro_ipc_msg_t* request,
                                     const void* payload,
                                     uint16_t len)
{
    jezgro_ipc_endpoint_state_t* src_ep = current_endpoint();
    if (src_ep == NULL) {
        return JEZGRO_IPC_ERR_INVALID;
    }

    g_stats.replies_sent++;

    return internal_send(src_ep->id,
                         request->header.src,
                         JEZGRO_IPC_MSG_REPLY,
                         request->header.seq,
                         payload, len);
}

/* ============================================================================
 * NOTIFICATIONS
 * ============================================================================ */

jezgro_ipc_error_t jezgro_ipc_notify(jezgro_endpoint_t dst,
                                      const void* payload,
                                      uint16_t len)
{
    jezgro_ipc_endpoint_state_t* src_ep = current_endpoint();
    if (src_ep == NULL) {
        /* Allow kernel to notify without endpoint */
        g_stats.notifications_sent++;
        return internal_send(JEZGRO_EP_KERNEL, dst,
                             JEZGRO_IPC_MSG_NOTIFY, 0,
                             payload, len);
    }

    g_stats.notifications_sent++;

    return internal_send(src_ep->id, dst,
                         JEZGRO_IPC_MSG_NOTIFY,
                         src_ep->seq_counter++,
                         payload, len);
}

void jezgro_ipc_set_notify_callback(jezgro_ipc_notify_cb callback)
{
    g_notify_callback = callback;
}

/* ============================================================================
 * INTERRUPT FORWARDING
 * ============================================================================ */

jezgro_ipc_error_t jezgro_ipc_forward_interrupt(uint32_t irq_num,
                                                  ekk_time_us_t timestamp)
{
    if (irq_num >= 64) {
        return JEZGRO_IPC_ERR_INVALID;
    }

    jezgro_endpoint_t dst = g_irq_endpoints[irq_num];
    if (dst == JEZGRO_EP_KERNEL) {
        /* Not registered for forwarding */
        return JEZGRO_IPC_OK;
    }

    /* Pack IRQ info */
    struct {
        uint32_t irq_num;
        uint64_t timestamp;
    } __attribute__((packed)) irq_data = {
        .irq_num = irq_num,
        .timestamp = timestamp
    };

    return internal_send(JEZGRO_EP_KERNEL, dst,
                         JEZGRO_IPC_MSG_INTERRUPT, 0,
                         &irq_data, sizeof(irq_data));
}

ekk_error_t jezgro_ipc_register_irq(uint32_t irq_num,
                                     jezgro_endpoint_t endpoint)
{
    if (irq_num >= 64) {
        return EKK_ERR_INVALID_ARG;
    }

    if (!jezgro_ipc_endpoint_exists(endpoint)) {
        return EKK_ERR_NOT_FOUND;
    }

    g_irq_endpoints[irq_num] = endpoint;
    return EKK_OK;
}

/* ============================================================================
 * CONTEXT MANAGEMENT
 * ============================================================================ */

/**
 * @brief Set current service context (called by scheduler)
 */
void jezgro_ipc_set_current_service(uint8_t service_id)
{
    g_current_service = service_id;
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

void jezgro_ipc_get_stats(jezgro_ipc_stats_t* stats)
{
    if (stats != NULL) {
        memcpy(stats, &g_stats, sizeof(jezgro_ipc_stats_t));
    }
}

void jezgro_ipc_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

/* ============================================================================
 * DEBUG
 * ============================================================================ */

void jezgro_ipc_dump_state(void)
{
    ekk_hal_printf("\nJEZGRO IPC State:\n");
    ekk_hal_printf("  Current service: %d\n", g_current_service);

    ekk_hal_printf("  Endpoints:\n");
    for (int i = 0; i < JEZGRO_IPC_MAX_ENDPOINTS; i++) {
        if (g_endpoints[i].active) {
            ekk_hal_printf("    EP %d: owner=%d, queue=%d/%d, waiting=%d\n",
                           g_endpoints[i].id,
                           g_endpoints[i].owner_service,
                           g_endpoints[i].rx_queue.count,
                           JEZGRO_IPC_QUEUE_DEPTH,
                           g_endpoints[i].waiting);
        }
    }

    ekk_hal_printf("  Statistics:\n");
    ekk_hal_printf("    Sent: %u, Received: %u\n",
                   g_stats.messages_sent, g_stats.messages_received);
    ekk_hal_printf("    Replies: %u, Notifications: %u\n",
                   g_stats.replies_sent, g_stats.notifications_sent);
    ekk_hal_printf("    Timeouts: %u, Overflows: %u\n",
                   g_stats.timeouts, g_stats.queue_overflows);
}
