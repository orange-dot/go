/*
 * ROJ Discovery - UDP broadcast-based peer discovery
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "discovery.h"

static char g_node_id[ROJ_NODE_ID_MAX];
static roj_lang_t g_lang;
static roj_peer_list_t g_peers;

int discovery_init(const char* node_id, roj_lang_t lang) {
    strncpy(g_node_id, node_id, ROJ_NODE_ID_MAX - 1);
    g_node_id[ROJ_NODE_ID_MAX - 1] = '\0';
    g_lang = lang;

    memset(&g_peers, 0, sizeof(g_peers));

    printf("[INFO] Discovery initialized for \"%s\" (%s)\n",
           g_node_id, lang_to_str(g_lang));

    return 0;
}

void discovery_shutdown(void) {
    memset(&g_peers, 0, sizeof(g_peers));
}

roj_peer_list_t* discovery_get_peers(void) {
    return &g_peers;
}

void discovery_update_peer(const char* node_id, roj_lang_t lang,
                          const struct sockaddr_in* addr, const char* version) {
    /* Don't add ourselves */
    if (strcmp(node_id, g_node_id) == 0) {
        return;
    }

    /* Check if peer already exists */
    for (int i = 0; i < g_peers.count; i++) {
        if (strcmp(g_peers.peers[i].node_id, node_id) == 0) {
            /* Update existing peer */
            g_peers.peers[i].lang = lang;
            g_peers.peers[i].addr = *addr;
            g_peers.peers[i].last_seen = time(NULL);
            if (version) {
                strncpy(g_peers.peers[i].version, version, 15);
            }
            return;
        }
    }

    /* Add new peer */
    if (g_peers.count < ROJ_MAX_PEERS) {
        roj_peer_t* peer = &g_peers.peers[g_peers.count];

        strncpy(peer->node_id, node_id, ROJ_NODE_ID_MAX - 1);
        peer->node_id[ROJ_NODE_ID_MAX - 1] = '\0';
        peer->lang = lang;
        peer->addr = *addr;
        peer->last_seen = time(NULL);
        peer->active = true;

        if (version) {
            strncpy(peer->version, version, 15);
            peer->version[15] = '\0';
        } else {
            strcpy(peer->version, ROJ_VERSION);
        }

        g_peers.count++;

        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, addr_str, sizeof(addr_str));

        printf("[INFO] mDNS: Discovered \"%s\" (%s) at %s:%d\n",
               node_id, lang_to_str(lang), addr_str, ntohs(addr->sin_port));
    }
}

int discovery_peer_count(void) {
    return g_peers.count;
}

int discovery_get_peer_addrs(struct sockaddr_in* addrs, int max_addrs) {
    int count = 0;
    for (int i = 0; i < g_peers.count && count < max_addrs; i++) {
        if (g_peers.peers[i].active) {
            addrs[count++] = g_peers.peers[i].addr;
        }
    }
    return count;
}
