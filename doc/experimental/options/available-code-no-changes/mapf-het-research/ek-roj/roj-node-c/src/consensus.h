/*
 * ROJ Consensus - K-threshold voting
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#ifndef ROJ_CONSENSUS_H
#define ROJ_CONSENSUS_H

#include "types.h"

#define ROJ_MAX_PROPOSALS 16
#define ROJ_MAX_STATE     64

/* Initialize consensus */
int consensus_init(const char* node_id);

/* Create a new proposal */
int consensus_create_proposal(const char* key, int64_t value, roj_message_t* msg);

/* Handle incoming PROPOSE, returns VOTE message */
int consensus_handle_propose(const roj_message_t* propose, roj_message_t* vote);

/* Handle incoming VOTE, returns COMMIT message if threshold reached */
int consensus_handle_vote(const roj_message_t* vote, roj_message_t* commit,
                          int peer_count);

/* Handle incoming COMMIT */
void consensus_handle_commit(const roj_message_t* commit);

/* Get committed state value (returns 0 if found, -1 if not) */
int consensus_get_state(const char* key, int64_t* value);

/* Print current state */
void consensus_print_state(void);

#endif /* ROJ_CONSENSUS_H */
