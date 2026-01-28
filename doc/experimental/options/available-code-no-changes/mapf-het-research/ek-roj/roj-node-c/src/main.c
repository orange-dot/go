/*
 * ROJ Node - C Implementation
 *
 * Distributed consensus node for the ROJ protocol.
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#define usleep(x) Sleep((x)/1000)
#else
#include <unistd.h>
#include <sys/select.h>
#endif

#include "types.h"
#include "discovery.h"
#include "transport.h"
#include "consensus.h"

static volatile int g_running = 1;
static char g_node_id[ROJ_NODE_ID_MAX];

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void print_help(void) {
    printf("\nCommands:\n");
    printf("  propose <key> <value>  - Propose a consensus value\n");
    printf("  state                  - Show committed state\n");
    printf("  peers                  - Show discovered peers\n");
    printf("  quit                   - Exit\n\n");
}

static void handle_stdin(void) {
    char line[256];
    if (fgets(line, sizeof(line), stdin) == NULL) {
        return;
    }

    /* Remove newline */
    line[strcspn(line, "\r\n")] = '\0';

    char cmd[64], key[64];
    int64_t value;

    if (sscanf(line, "propose %63s %lld", key, (long long*)&value) == 2) {
        roj_message_t msg;
        if (consensus_create_proposal(key, value, &msg) == 0) {
            struct sockaddr_in addrs[ROJ_MAX_PEERS];
            int count = discovery_get_peer_addrs(addrs, ROJ_MAX_PEERS);

            if (count == 0) {
                printf("[INFO] No peers discovered yet\n");
            } else {
                transport_broadcast(&msg, addrs, count);
            }
        }
    }
    else if (strncmp(line, "state", 5) == 0) {
        consensus_print_state();
    }
    else if (strncmp(line, "peers", 5) == 0) {
        roj_peer_list_t* peers = discovery_get_peers();
        printf("Discovered peers:\n");
        if (peers->count == 0) {
            printf("  (none)\n");
        } else {
            for (int i = 0; i < peers->count; i++) {
                if (peers->peers[i].active) {
                    char addr_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &peers->peers[i].addr.sin_addr,
                              addr_str, sizeof(addr_str));
                    printf("  %s (%s) at %s:%d\n",
                           peers->peers[i].node_id,
                           lang_to_str(peers->peers[i].lang),
                           addr_str,
                           ntohs(peers->peers[i].addr.sin_port));
                }
            }
        }
    }
    else if (strncmp(line, "quit", 4) == 0 || strncmp(line, "exit", 4) == 0) {
        g_running = 0;
    }
    else if (strlen(line) > 0) {
        printf("Unknown command. Try: propose <key> <value>\n");
    }
}

static void handle_message(const roj_message_t* msg, const struct sockaddr_in* from) {
    switch (msg->type) {
        case MSG_ANNOUNCE:
            /* Update peer list */
            discovery_update_peer(msg->data.announce.node_id,
                                  msg->data.announce.lang,
                                  from,
                                  msg->data.announce.version);
            break;

        case MSG_PROPOSE:
            if (strcmp(msg->data.propose.from, g_node_id) != 0) {
                roj_message_t vote;
                if (consensus_handle_propose(msg, &vote) == 0) {
                    transport_send(&vote, from);
                }
            }
            break;

        case MSG_VOTE:
            if (strcmp(msg->data.vote.from, g_node_id) != 0) {
                roj_message_t commit;
                int peer_count = discovery_peer_count();
                if (consensus_handle_vote(msg, &commit, peer_count) == 0) {
                    /* Broadcast commit */
                    struct sockaddr_in addrs[ROJ_MAX_PEERS];
                    int count = discovery_get_peer_addrs(addrs, ROJ_MAX_PEERS);
                    transport_broadcast(&commit, addrs, count);
                }
            }
            break;

        case MSG_COMMIT:
            consensus_handle_commit(msg);
            break;

        default:
            break;
    }
}

int main(int argc, char* argv[]) {
    int port = ROJ_UDP_PORT;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--name") == 0 || strcmp(argv[i], "-n") == 0) && i + 1 < argc) {
            strncpy(g_node_id, argv[++i], ROJ_NODE_ID_MAX - 1);
        }
        else if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s --name <node_id> [--port <port>]\n", argv[0]);
            return 0;
        }
    }

    if (strlen(g_node_id) == 0) {
        fprintf(stderr, "Error: --name is required\n");
        fprintf(stderr, "Usage: %s --name <node_id> [--port <port>]\n", argv[0]);
        return 1;
    }

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
#endif

    printf("[INFO] ROJ node \"%s\" starting (c)\n", g_node_id);

    /* Initialize subsystems */
    if (discovery_init(g_node_id, LANG_C) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize discovery\n");
        return 1;
    }

    if (transport_init(port) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize transport\n");
        return 1;
    }

    if (consensus_init(g_node_id) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize consensus\n");
        return 1;
    }

    print_help();

    int sock = transport_get_socket();
    fd_set readfds;
    struct timeval tv;

    /* Main event loop */
    while (g_running) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
#ifndef _WIN32
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;
#else
        int maxfd = sock;
        /* On Windows, check stdin separately */
        if (_kbhit()) {
            handle_stdin();
        }
#endif

        tv.tv_sec = 0;
        tv.tv_usec = 100000;  /* 100ms timeout */

        int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            break;
        }

        if (ret > 0) {
            if (FD_ISSET(sock, &readfds)) {
                roj_message_t msg;
                struct sockaddr_in from;
                if (transport_recv(&msg, &from) == 0) {
                    handle_message(&msg, &from);
                }
            }

#ifndef _WIN32
            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                handle_stdin();
            }
#endif
        }
    }

    printf("\n[INFO] Shutting down...\n");

    transport_shutdown();
    discovery_shutdown();

    return 0;
}
