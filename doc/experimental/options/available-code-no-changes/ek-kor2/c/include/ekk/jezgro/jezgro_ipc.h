/**
 * @file jezgro_ipc.h
 * @brief JEZGRO Microkernel - Inter-Process Communication
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license AGPL-3.0
 *
 * Provides message-passing IPC for service-to-service and service-to-kernel
 * communication in the JEZGRO microkernel.
 *
 * DESIGN PRINCIPLES:
 * 1. Zero-copy for efficiency (messages stay in shared buffer)
 * 2. Fixed 64-byte messages (fits in one CAN-FD frame)
 * 3. Synchronous send/receive with timeout
 * 4. Async notifications for events
 *
 * MESSAGE TYPES:
 * - Request/Reply: Synchronous call-return pattern
 * - Notification: Async one-way event
 * - Interrupt Forward: Kernel forwards interrupt to user service
 *
 * PAPER REFERENCE:
 * ROJ Paper Section IV.B: "JEZGRO uses message-passing IPC to maintain
 * isolation while enabling efficient coordination between LLC control
 * and ROJ coordination layers"
 */

#ifndef JEZGRO_IPC_H
#define JEZGRO_IPC_H

#include "../ekk_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/** Maximum message payload size (bytes) */
#define JEZGRO_IPC_MSG_SIZE         64

/** Maximum number of pending messages per endpoint */
#define JEZGRO_IPC_QUEUE_DEPTH      16

/** Maximum number of IPC endpoints */
#define JEZGRO_IPC_MAX_ENDPOINTS    16

/** Timeout for infinite wait */
#define JEZGRO_IPC_WAIT_FOREVER     0xFFFFFFFF

/** Default timeout (ms) */
#define JEZGRO_IPC_DEFAULT_TIMEOUT  100

/* ============================================================================
 * MESSAGE TYPES
 * ============================================================================ */

/**
 * @brief IPC message type
 */
typedef enum {
    JEZGRO_IPC_MSG_REQUEST    = 0x01,  /**< Request expecting reply */
    JEZGRO_IPC_MSG_REPLY      = 0x02,  /**< Reply to request */
    JEZGRO_IPC_MSG_NOTIFY     = 0x03,  /**< Async notification */
    JEZGRO_IPC_MSG_INTERRUPT  = 0x04,  /**< Interrupt forward */
    JEZGRO_IPC_MSG_SHUTDOWN   = 0x05,  /**< Service shutdown request */
    JEZGRO_IPC_MSG_HEARTBEAT  = 0x06,  /**< Liveness check */
} jezgro_ipc_msg_type_t;

/**
 * @brief IPC error codes
 */
typedef enum {
    JEZGRO_IPC_OK             = 0,
    JEZGRO_IPC_ERR_TIMEOUT    = -1,
    JEZGRO_IPC_ERR_INVALID    = -2,
    JEZGRO_IPC_ERR_FULL       = -3,
    JEZGRO_IPC_ERR_EMPTY      = -4,
    JEZGRO_IPC_ERR_NOT_FOUND  = -5,
    JEZGRO_IPC_ERR_DEAD       = -6,  /**< Target service is dead */
    JEZGRO_IPC_ERR_DENIED     = -7,  /**< Permission denied */
} jezgro_ipc_error_t;

/* ============================================================================
 * ENDPOINT IDENTIFIERS
 * ============================================================================ */

/** Endpoint ID type */
typedef uint8_t jezgro_endpoint_t;

/** Well-known kernel endpoints */
#define JEZGRO_EP_KERNEL        0x00  /**< Kernel services */
#define JEZGRO_EP_REINCARNATION 0x01  /**< Reincarnation server */
#define JEZGRO_EP_LLC           0x02  /**< LLC control service */
#define JEZGRO_EP_ROJ           0x03  /**< ROJ coordination service */
#define JEZGRO_EP_DIAGNOSTICS   0x04  /**< Diagnostics service */
#define JEZGRO_EP_BROADCAST     0xFF  /**< Broadcast to all */

/* ============================================================================
 * MESSAGE STRUCTURE
 * ============================================================================ */

/**
 * @brief IPC message header (8 bytes)
 */
typedef struct {
    jezgro_endpoint_t src;       /**< Source endpoint */
    jezgro_endpoint_t dst;       /**< Destination endpoint */
    uint8_t type;                /**< Message type */
    uint8_t flags;               /**< Flags (TBD) */
    uint16_t seq;                /**< Sequence number */
    uint16_t len;                /**< Payload length */
} __attribute__((packed)) jezgro_ipc_header_t;

/**
 * @brief IPC message (64 bytes total)
 */
typedef struct {
    jezgro_ipc_header_t header;                 /**< 8-byte header */
    uint8_t payload[JEZGRO_IPC_MSG_SIZE - 8];   /**< 56-byte payload */
} __attribute__((packed)) jezgro_ipc_msg_t;

_Static_assert(sizeof(jezgro_ipc_msg_t) == 64, "Message must be 64 bytes");

/* ============================================================================
 * MESSAGE QUEUE
 * ============================================================================ */

/**
 * @brief Message queue (ring buffer)
 */
typedef struct {
    jezgro_ipc_msg_t messages[JEZGRO_IPC_QUEUE_DEPTH];
    volatile uint8_t head;       /**< Write index */
    volatile uint8_t tail;       /**< Read index */
    volatile uint8_t count;      /**< Number of messages */
} jezgro_ipc_queue_t;

/**
 * @brief IPC endpoint state
 */
typedef struct {
    jezgro_endpoint_t id;        /**< Endpoint ID */
    uint8_t owner_service;       /**< Owning service ID */
    bool active;                 /**< Endpoint is registered */
    jezgro_ipc_queue_t rx_queue; /**< Receive queue */
    uint16_t seq_counter;        /**< Sequence counter */

    /* Blocking receive support */
    bool waiting;                /**< Service waiting for message */
    uint32_t wait_deadline;      /**< Deadline (0 = forever) */
} jezgro_ipc_endpoint_state_t;

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize JEZGRO IPC subsystem
 *
 * @return EKK_OK on success
 */
ekk_error_t jezgro_ipc_init(void);

/* ============================================================================
 * ENDPOINT MANAGEMENT
 * ============================================================================ */

/**
 * @brief Register an IPC endpoint
 *
 * Creates a message queue for receiving messages.
 *
 * @param endpoint Desired endpoint ID
 * @param service_id Owning service ID
 * @return EKK_OK on success, EKK_ERR_LIMIT if too many endpoints
 */
ekk_error_t jezgro_ipc_register_endpoint(jezgro_endpoint_t endpoint,
                                          uint8_t service_id);

/**
 * @brief Unregister an IPC endpoint
 *
 * @param endpoint Endpoint to unregister
 * @return EKK_OK on success
 */
ekk_error_t jezgro_ipc_unregister_endpoint(jezgro_endpoint_t endpoint);

/**
 * @brief Check if endpoint exists and is active
 *
 * @param endpoint Endpoint to check
 * @return true if endpoint is valid and active
 */
bool jezgro_ipc_endpoint_exists(jezgro_endpoint_t endpoint);

/* ============================================================================
 * SYNCHRONOUS MESSAGE PASSING
 * ============================================================================ */

/**
 * @brief Send a message and wait for reply
 *
 * Blocking call that sends request and waits for reply.
 * Used for synchronous service calls.
 *
 * @param dst Destination endpoint
 * @param request Request message (payload only, header filled in)
 * @param request_len Request payload length
 * @param reply Buffer for reply payload
 * @param reply_len On input: max reply size; on output: actual size
 * @param timeout_ms Timeout in milliseconds
 * @return JEZGRO_IPC_OK on success, error code otherwise
 */
jezgro_ipc_error_t jezgro_ipc_call(jezgro_endpoint_t dst,
                                    const void* request,
                                    uint16_t request_len,
                                    void* reply,
                                    uint16_t* reply_len,
                                    uint32_t timeout_ms);

/**
 * @brief Send a message without waiting for reply
 *
 * Non-blocking send. Message is queued for delivery.
 *
 * @param dst Destination endpoint
 * @param type Message type
 * @param payload Message payload
 * @param len Payload length
 * @return JEZGRO_IPC_OK on success
 */
jezgro_ipc_error_t jezgro_ipc_send(jezgro_endpoint_t dst,
                                    jezgro_ipc_msg_type_t type,
                                    const void* payload,
                                    uint16_t len);

/**
 * @brief Receive a message
 *
 * Blocking receive with timeout.
 *
 * @param msg Buffer for received message
 * @param timeout_ms Timeout in milliseconds
 * @return JEZGRO_IPC_OK on success, JEZGRO_IPC_ERR_TIMEOUT if timed out
 */
jezgro_ipc_error_t jezgro_ipc_recv(jezgro_ipc_msg_t* msg,
                                    uint32_t timeout_ms);

/**
 * @brief Non-blocking receive
 *
 * @param msg Buffer for received message
 * @return JEZGRO_IPC_OK if message received, JEZGRO_IPC_ERR_EMPTY if none
 */
jezgro_ipc_error_t jezgro_ipc_recv_nonblock(jezgro_ipc_msg_t* msg);

/**
 * @brief Send reply to a request
 *
 * @param request Original request message (for src/seq matching)
 * @param payload Reply payload
 * @param len Payload length
 * @return JEZGRO_IPC_OK on success
 */
jezgro_ipc_error_t jezgro_ipc_reply(const jezgro_ipc_msg_t* request,
                                     const void* payload,
                                     uint16_t len);

/* ============================================================================
 * ASYNC NOTIFICATIONS
 * ============================================================================ */

/**
 * @brief Send async notification
 *
 * Fire-and-forget notification. Does not wait for ACK.
 *
 * @param dst Destination endpoint (or JEZGRO_EP_BROADCAST)
 * @param payload Notification data
 * @param len Payload length
 * @return JEZGRO_IPC_OK on success
 */
jezgro_ipc_error_t jezgro_ipc_notify(jezgro_endpoint_t dst,
                                      const void* payload,
                                      uint16_t len);

/**
 * @brief Notification callback type
 */
typedef void (*jezgro_ipc_notify_cb)(jezgro_endpoint_t src,
                                      const void* payload,
                                      uint16_t len);

/**
 * @brief Register notification callback
 *
 * Callback is invoked asynchronously when notification arrives.
 *
 * @param callback Notification handler
 */
void jezgro_ipc_set_notify_callback(jezgro_ipc_notify_cb callback);

/* ============================================================================
 * INTERRUPT FORWARDING
 * ============================================================================ */

/**
 * @brief Forward interrupt to user service
 *
 * Called from kernel interrupt handler to forward interrupt
 * to registered user service.
 *
 * @param irq_num Interrupt number
 * @param timestamp Interrupt timestamp
 * @return JEZGRO_IPC_OK if forwarded
 */
jezgro_ipc_error_t jezgro_ipc_forward_interrupt(uint32_t irq_num,
                                                  ekk_time_us_t timestamp);

/**
 * @brief Register interrupt handler endpoint
 *
 * @param irq_num Interrupt number
 * @param endpoint Endpoint to forward to
 * @return EKK_OK on success
 */
ekk_error_t jezgro_ipc_register_irq(uint32_t irq_num,
                                     jezgro_endpoint_t endpoint);

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

/**
 * @brief IPC statistics
 */
typedef struct {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t replies_sent;
    uint32_t notifications_sent;
    uint32_t timeouts;
    uint32_t queue_overflows;
    uint32_t dead_service_errors;
} jezgro_ipc_stats_t;

/**
 * @brief Get IPC statistics
 *
 * @param stats Output buffer
 */
void jezgro_ipc_get_stats(jezgro_ipc_stats_t* stats);

/**
 * @brief Reset IPC statistics
 */
void jezgro_ipc_reset_stats(void);

/* ============================================================================
 * DEBUG
 * ============================================================================ */

/**
 * @brief Dump IPC state to debug output
 */
void jezgro_ipc_dump_state(void);

#ifdef __cplusplus
}
#endif

#endif /* JEZGRO_IPC_H */
