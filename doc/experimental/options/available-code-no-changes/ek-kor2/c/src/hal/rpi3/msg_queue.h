/**
 * @file msg_queue.h
 * @brief Per-Core Message Queue Header for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_MSG_QUEUE_H
#define RPI3_MSG_QUEUE_H

#include <stdint.h>

/* Broadcast destination (send to all other cores) */
#define MSG_BROADCAST       0xFF

/**
 * @brief Message receive callback type
 */
typedef void (*msg_recv_callback_t)(uint8_t sender_id,
                                    uint8_t msg_type,
                                    const void *data,
                                    uint32_t len);

/**
 * @brief Initialize message queues (call from each core)
 */
void msg_queue_init(void);

/**
 * @brief Send message to a specific core or broadcast
 *
 * @param dest_core Destination core (0-3) or MSG_BROADCAST
 * @param sender_id Sender's module ID
 * @param msg_type Message type
 * @param data Payload
 * @param len Payload length (max 64)
 * @return 0 on success, -1 on error
 */
int msg_queue_send(uint32_t dest_core, uint8_t sender_id,
                   uint8_t msg_type, const void *data, uint32_t len);

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
                   void *data, uint32_t *len);

/**
 * @brief Check if this core's queue has messages
 * @return Non-zero if messages available
 */
int msg_queue_has_message(void);

/**
 * @brief Get number of messages in this core's queue
 * @return Number of messages
 */
uint32_t msg_queue_count(void);

/**
 * @brief Set receive callback
 * @param callback Function to call on message receive
 */
void msg_queue_set_callback(msg_recv_callback_t callback);

/**
 * @brief Poll for messages and invoke callback
 * @return Number of messages processed
 */
uint32_t msg_queue_poll(void);

#endif /* RPI3_MSG_QUEUE_H */
