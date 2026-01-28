/*
 * ROJ Transport - UDP messaging
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#ifndef ROJ_TRANSPORT_H
#define ROJ_TRANSPORT_H

#include "types.h"

/* Initialize transport on specified port */
int transport_init(int port);

/* Shutdown transport */
void transport_shutdown(void);

/* Get socket file descriptor for select() */
int transport_get_socket(void);

/* Receive a message (non-blocking if used with select) */
int transport_recv(roj_message_t* msg, struct sockaddr_in* from);

/* Send a message to specific address */
int transport_send(const roj_message_t* msg, const struct sockaddr_in* to);

/* Broadcast a message to multiple addresses */
int transport_broadcast(const roj_message_t* msg,
                       const struct sockaddr_in* addrs, int addr_count);

/* Serialize message to JSON */
int message_to_json(const roj_message_t* msg, char* buf, size_t buf_size);

/* Parse message from JSON */
int message_from_json(const char* json, roj_message_t* msg);

#endif /* ROJ_TRANSPORT_H */
