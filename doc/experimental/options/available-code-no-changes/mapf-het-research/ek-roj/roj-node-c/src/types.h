/*
 * ROJ Protocol Types - C Implementation
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#ifndef ROJ_TYPES_H
#define ROJ_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

/* Constants */
#define ROJ_NODE_ID_MAX     64
#define ROJ_KEY_MAX         64
#define ROJ_VERSION         "0.1.0"
#define ROJ_UDP_PORT        9990
#define ROJ_MAX_PEERS       32
#define ROJ_MAX_VOTERS      16
#define ROJ_PROPOSAL_ID_LEN 9
#define ROJ_MSG_MAX_SIZE    65536
#define ROJ_VOTE_THRESHOLD  0.67

/* Language enum */
typedef enum {
    LANG_RUST = 0,
    LANG_GO,
    LANG_C
} roj_lang_t;

static inline const char* lang_to_str(roj_lang_t lang) {
    switch (lang) {
        case LANG_RUST: return "rust";
        case LANG_GO:   return "go";
        case LANG_C:    return "c";
        default:        return "unknown";
    }
}

static inline roj_lang_t str_to_lang(const char* s) {
    if (!s) return LANG_C;
    if (strcmp(s, "rust") == 0) return LANG_RUST;
    if (strcmp(s, "go") == 0) return LANG_GO;
    return LANG_C;
}

/* Vote enum */
typedef enum {
    VOTE_ACCEPT = 0,
    VOTE_REJECT
} roj_vote_t;

static inline const char* vote_to_str(roj_vote_t vote) {
    return vote == VOTE_ACCEPT ? "accept" : "reject";
}

static inline roj_vote_t str_to_vote(const char* s) {
    if (s && strcmp(s, "reject") == 0) return VOTE_REJECT;
    return VOTE_ACCEPT;
}

/* Message types */
typedef enum {
    MSG_ANNOUNCE = 0,
    MSG_PROPOSE,
    MSG_VOTE,
    MSG_COMMIT,
    MSG_UNKNOWN
} roj_msg_type_t;

/* Peer information */
typedef struct {
    char node_id[ROJ_NODE_ID_MAX];
    roj_lang_t lang;
    struct sockaddr_in addr;
    char version[16];
    time_t last_seen;
    bool active;
} roj_peer_t;

/* Peer list */
typedef struct {
    roj_peer_t peers[ROJ_MAX_PEERS];
    int count;
} roj_peer_list_t;

/* Vote record */
typedef struct {
    char node_id[ROJ_NODE_ID_MAX];
    roj_vote_t vote;
} roj_vote_record_t;

/* Proposal state */
typedef struct {
    char proposal_id[ROJ_PROPOSAL_ID_LEN];
    char key[ROJ_KEY_MAX];
    int64_t value;
    int64_t timestamp;
    roj_vote_record_t votes[ROJ_MAX_VOTERS];
    int vote_count;
    bool active;
} roj_proposal_t;

/* State entry */
typedef struct {
    char key[ROJ_KEY_MAX];
    int64_t value;
} roj_state_entry_t;

/* Message structures */
typedef struct {
    roj_msg_type_t type;
    union {
        /* ANNOUNCE */
        struct {
            char node_id[ROJ_NODE_ID_MAX];
            roj_lang_t lang;
            char version[16];
        } announce;

        /* PROPOSE */
        struct {
            char proposal_id[ROJ_PROPOSAL_ID_LEN];
            char from[ROJ_NODE_ID_MAX];
            char key[ROJ_KEY_MAX];
            int64_t value;
            int64_t timestamp;
        } propose;

        /* VOTE */
        struct {
            char proposal_id[ROJ_PROPOSAL_ID_LEN];
            char from[ROJ_NODE_ID_MAX];
            roj_vote_t vote;
        } vote;

        /* COMMIT */
        struct {
            char proposal_id[ROJ_PROPOSAL_ID_LEN];
            char key[ROJ_KEY_MAX];
            int64_t value;
            char voters[ROJ_MAX_VOTERS][ROJ_NODE_ID_MAX];
            int voter_count;
        } commit;
    } data;
} roj_message_t;

#endif /* ROJ_TYPES_H */
