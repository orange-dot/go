/*
 * ROJ Discovery - UDP broadcast-based discovery (simplified)
 *
 * Note: Uses UDP broadcast instead of mDNS for simplicity on Windows.
 * mDNS can be added later using the mdns.h single-header library.
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#ifndef ROJ_DISCOVERY_H
#define ROJ_DISCOVERY_H

#include "types.h"

/* Initialize discovery subsystem */
int discovery_init(const char* node_id, roj_lang_t lang);

/* Shutdown discovery */
void discovery_shutdown(void);

/* Get peer list */
roj_peer_list_t* discovery_get_peers(void);

/* Add/update a peer from an ANNOUNCE message */
void discovery_update_peer(const char* node_id, roj_lang_t lang,
                          const struct sockaddr_in* addr, const char* version);

/* Get peer count */
int discovery_peer_count(void);

/* Get addresses for broadcasting */
int discovery_get_peer_addrs(struct sockaddr_in* addrs, int max_addrs);

#endif /* ROJ_DISCOVERY_H */
