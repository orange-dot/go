/*
 * ROJ Consensus - K-threshold voting implementation
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "consensus.h"

static char g_node_id[ROJ_NODE_ID_MAX];
static roj_proposal_t g_proposals[ROJ_MAX_PROPOSALS];
static roj_state_entry_t g_state[ROJ_MAX_STATE];
static int g_state_count = 0;
static int g_proposal_counter = 0;

int consensus_init(const char* node_id) {
    strncpy(g_node_id, node_id, ROJ_NODE_ID_MAX - 1);
    g_node_id[ROJ_NODE_ID_MAX - 1] = '\0';

    memset(g_proposals, 0, sizeof(g_proposals));
    memset(g_state, 0, sizeof(g_state));
    g_state_count = 0;
    g_proposal_counter = 0;

    return 0;
}

static roj_proposal_t* find_proposal(const char* proposal_id) {
    for (int i = 0; i < ROJ_MAX_PROPOSALS; i++) {
        if (g_proposals[i].active &&
            strcmp(g_proposals[i].proposal_id, proposal_id) == 0) {
            return &g_proposals[i];
        }
    }
    return NULL;
}

static roj_proposal_t* alloc_proposal(void) {
    for (int i = 0; i < ROJ_MAX_PROPOSALS; i++) {
        if (!g_proposals[i].active) {
            return &g_proposals[i];
        }
    }
    return NULL;
}

static void generate_proposal_id(char* buf) {
    /* Simple incrementing ID with random component */
    unsigned int r = (unsigned int)time(NULL) ^ g_proposal_counter++;
    snprintf(buf, ROJ_PROPOSAL_ID_LEN, "%08x", r);
}

int consensus_create_proposal(const char* key, int64_t value, roj_message_t* msg) {
    roj_proposal_t* p = alloc_proposal();
    if (!p) {
        fprintf(stderr, "[WARN] No space for new proposal\n");
        return -1;
    }

    memset(p, 0, sizeof(*p));
    generate_proposal_id(p->proposal_id);
    strncpy(p->key, key, ROJ_KEY_MAX - 1);
    p->value = value;
    p->timestamp = (int64_t)time(NULL);
    p->active = true;

    printf("[INFO] Consensus: Proposing %s=%lld (id=%s)\n",
           key, (long long)value, p->proposal_id);

    /* Create PROPOSE message */
    memset(msg, 0, sizeof(*msg));
    msg->type = MSG_PROPOSE;
    strcpy(msg->data.propose.proposal_id, p->proposal_id);
    strcpy(msg->data.propose.from, g_node_id);
    strcpy(msg->data.propose.key, key);
    msg->data.propose.value = value;
    msg->data.propose.timestamp = p->timestamp;

    return 0;
}

int consensus_handle_propose(const roj_message_t* propose, roj_message_t* vote) {
    printf("[INFO] Consensus: Received PROPOSE %s=%lld from %s\n",
           propose->data.propose.key,
           (long long)propose->data.propose.value,
           propose->data.propose.from);

    /* Store proposal */
    roj_proposal_t* p = alloc_proposal();
    if (p) {
        memset(p, 0, sizeof(*p));
        strcpy(p->proposal_id, propose->data.propose.proposal_id);
        strcpy(p->key, propose->data.propose.key);
        p->value = propose->data.propose.value;
        p->timestamp = propose->data.propose.timestamp;
        p->active = true;
    }

    /* Always accept for demo */
    printf("[INFO] Consensus: VOTE accept for %s (2/3 threshold)\n",
           propose->data.propose.proposal_id);

    /* Create VOTE message */
    memset(vote, 0, sizeof(*vote));
    vote->type = MSG_VOTE;
    strcpy(vote->data.vote.proposal_id, propose->data.propose.proposal_id);
    strcpy(vote->data.vote.from, g_node_id);
    vote->data.vote.vote = VOTE_ACCEPT;

    return 0;
}

int consensus_handle_vote(const roj_message_t* vote_msg, roj_message_t* commit,
                          int peer_count) {
    printf("[INFO] Consensus: Received VOTE %s from %s for %s\n",
           vote_to_str(vote_msg->data.vote.vote),
           vote_msg->data.vote.from,
           vote_msg->data.vote.proposal_id);

    roj_proposal_t* p = find_proposal(vote_msg->data.vote.proposal_id);
    if (!p) {
        return -1;
    }

    /* Record vote */
    if (p->vote_count < ROJ_MAX_VOTERS) {
        strncpy(p->votes[p->vote_count].node_id, vote_msg->data.vote.from,
                ROJ_NODE_ID_MAX - 1);
        p->votes[p->vote_count].vote = vote_msg->data.vote.vote;
        p->vote_count++;
    }

    /* Count accepts */
    int accept_count = 0;
    for (int i = 0; i < p->vote_count; i++) {
        if (p->votes[i].vote == VOTE_ACCEPT) {
            accept_count++;
        }
    }

    int total = peer_count + 1;  /* Include ourselves */
    int threshold = (int)(total * ROJ_VOTE_THRESHOLD + 0.5);

    printf("[INFO] Consensus: %d/%d votes (%d needed for threshold)\n",
           accept_count, total, threshold);

    if (accept_count >= threshold) {
        /* Commit locally */
        if (g_state_count < ROJ_MAX_STATE) {
            /* Check if key exists */
            int found = -1;
            for (int i = 0; i < g_state_count; i++) {
                if (strcmp(g_state[i].key, p->key) == 0) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                g_state[found].value = p->value;
            } else {
                strcpy(g_state[g_state_count].key, p->key);
                g_state[g_state_count].value = p->value;
                g_state_count++;
            }
        }

        printf("[INFO] Consensus: COMMIT %s=%lld\n", p->key, (long long)p->value);

        /* Create COMMIT message */
        memset(commit, 0, sizeof(*commit));
        commit->type = MSG_COMMIT;
        strcpy(commit->data.commit.proposal_id, p->proposal_id);
        strcpy(commit->data.commit.key, p->key);
        commit->data.commit.value = p->value;

        /* Add voters */
        commit->data.commit.voter_count = 0;
        for (int i = 0; i < p->vote_count && commit->data.commit.voter_count < ROJ_MAX_VOTERS; i++) {
            if (p->votes[i].vote == VOTE_ACCEPT) {
                strcpy(commit->data.commit.voters[commit->data.commit.voter_count],
                       p->votes[i].node_id);
                commit->data.commit.voter_count++;
            }
        }

        /* Clear proposal */
        p->active = false;

        return 0;  /* Have commit message */
    }

    return -1;  /* No commit yet */
}

void consensus_handle_commit(const roj_message_t* commit) {
    printf("[INFO] Consensus: COMMIT %s=%lld (voters: ",
           commit->data.commit.key, (long long)commit->data.commit.value);

    for (int i = 0; i < commit->data.commit.voter_count; i++) {
        printf("%s%s", i > 0 ? ", " : "", commit->data.commit.voters[i]);
    }
    printf(")\n");

    /* Apply to local state */
    int found = -1;
    for (int i = 0; i < g_state_count; i++) {
        if (strcmp(g_state[i].key, commit->data.commit.key) == 0) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        g_state[found].value = commit->data.commit.value;
    } else if (g_state_count < ROJ_MAX_STATE) {
        strcpy(g_state[g_state_count].key, commit->data.commit.key);
        g_state[g_state_count].value = commit->data.commit.value;
        g_state_count++;
    }

    /* Clear any matching proposal */
    roj_proposal_t* p = find_proposal(commit->data.commit.proposal_id);
    if (p) {
        p->active = false;
    }
}

int consensus_get_state(const char* key, int64_t* value) {
    for (int i = 0; i < g_state_count; i++) {
        if (strcmp(g_state[i].key, key) == 0) {
            *value = g_state[i].value;
            return 0;
        }
    }
    return -1;
}

void consensus_print_state(void) {
    printf("Committed state:\n");
    if (g_state_count == 0) {
        printf("  (empty)\n");
        return;
    }
    for (int i = 0; i < g_state_count; i++) {
        printf("  %s = %lld\n", g_state[i].key, (long long)g_state[i].value);
    }
}
