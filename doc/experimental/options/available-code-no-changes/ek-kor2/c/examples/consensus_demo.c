/**
 * @file consensus_demo.c
 * @brief EK-KOR v2 Consensus Example - Distributed Voting Demo
 *
 * Demonstrates:
 * - 5 modules participate in consensus voting
 * - One module proposes MODE_CHANGE
 * - Others vote (approve/reject)
 * - Threshold logic (supermajority 67%)
 * - Inhibition of competing proposals
 *
 * Run with: ./example_consensus
 *
 * Copyright (c) 2026 Elektrokombinacija
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ekk/ekk.h"

/* Configuration */
#define NUM_MODULES         5
#define TICK_INTERVAL_US    10000   /* 10ms */
#define VOTE_ROUNDS         3       /* Number of voting rounds to demo */

/* Module states */
static ekk_module_t g_modules[NUM_MODULES];
static char g_module_names[NUM_MODULES][16];

/* Vote decision strategies for each module */
typedef enum {
    STRATEGY_ALWAYS_YES,
    STRATEGY_ALWAYS_NO,
    STRATEGY_RANDOM,
    STRATEGY_FOLLOW_LOAD,  /* Yes if load gradient positive */
} vote_strategy_t;

static vote_strategy_t g_strategies[NUM_MODULES] = {
    STRATEGY_ALWAYS_YES,    /* Module 1: Proposer, votes yes */
    STRATEGY_ALWAYS_YES,    /* Module 2: Yes */
    STRATEGY_ALWAYS_YES,    /* Module 3: Yes */
    STRATEGY_ALWAYS_NO,     /* Module 4: No (dissenter) */
    STRATEGY_FOLLOW_LOAD,   /* Module 5: Conditional */
};

/**
 * @brief Vote result name helper
 */
static const char* result_name(ekk_vote_result_t result)
{
    switch (result) {
        case EKK_VOTE_PENDING:   return "PENDING";
        case EKK_VOTE_APPROVED:  return "APPROVED";
        case EKK_VOTE_REJECTED:  return "REJECTED";
        case EKK_VOTE_TIMEOUT:   return "TIMEOUT";
        case EKK_VOTE_CANCELLED: return "CANCELLED";
        default:                 return "???";
    }
}

/**
 * @brief Vote value name helper
 */
static const char* vote_name(ekk_vote_value_t vote)
{
    switch (vote) {
        case EKK_VOTE_ABSTAIN: return "ABSTAIN";
        case EKK_VOTE_YES:     return "YES";
        case EKK_VOTE_NO:      return "NO";
        case EKK_VOTE_INHIBIT: return "INHIBIT";
        default:               return "???";
    }
}

/**
 * @brief Decide vote based on module's strategy
 */
static ekk_vote_value_t decide_vote(int module_idx, const ekk_ballot_t *ballot)
{
    ekk_module_t *m = &g_modules[module_idx];

    switch (g_strategies[module_idx]) {
        case STRATEGY_ALWAYS_YES:
            return EKK_VOTE_YES;

        case STRATEGY_ALWAYS_NO:
            return EKK_VOTE_NO;

        case STRATEGY_RANDOM:
            return (rand() % 2) ? EKK_VOTE_YES : EKK_VOTE_NO;

        case STRATEGY_FOLLOW_LOAD:
            /* Vote yes if we have high load (need mode change) */
            if (m->my_field.components[EKK_FIELD_LOAD] > EKK_FIXED_HALF) {
                return EKK_VOTE_YES;
            }
            return EKK_VOTE_NO;

        default:
            return EKK_VOTE_ABSTAIN;
    }
}

/**
 * @brief Simulate discovery and neighbor setup
 */
static void setup_topology(void)
{
    for (int i = 0; i < NUM_MODULES; i++) {
        for (int j = 0; j < NUM_MODULES; j++) {
            if (i != j) {
                ekk_topology_on_discovery(&g_modules[j].topology,
                                          g_modules[i].id,
                                          g_modules[i].topology.my_position);
            }
        }
    }

    /* Reelect neighbors */
    for (int i = 0; i < NUM_MODULES; i++) {
        ekk_topology_reelect(&g_modules[i].topology);
    }
}

/**
 * @brief Run a voting round
 */
static void run_voting_round(int round, uint32_t proposal_data, ekk_fixed_t threshold)
{
    ekk_error_t err;
    ekk_ballot_id_t ballot_id;
    ekk_time_us_t now = ekk_hal_time_us();

    printf("\n--- Voting Round %d ---\n", round);
    printf("Proposal: MODE_CHANGE to mode %u\n", proposal_data);
    printf("Threshold: %.0f%% (supermajority)\n",
           EKK_FIXED_TO_FLOAT(threshold) * 100.0f);

    /* Set module loads (affects FOLLOW_LOAD strategy) */
    if (round == 2) {
        /* Round 2: Module 5 has high load, will vote yes */
        printf("(Module 5 has high load this round)\n");
        g_modules[4].my_field.components[EKK_FIELD_LOAD] = EKK_FLOAT_TO_FIXED(0.8f);
    } else {
        /* Other rounds: Module 5 has low load, will vote no */
        g_modules[4].my_field.components[EKK_FIELD_LOAD] = EKK_FLOAT_TO_FIXED(0.2f);
    }

    /* Module 1 (index 0) proposes */
    printf("\nModule %u proposes...\n", g_modules[0].id);

    err = ekk_consensus_propose(&g_modules[0].consensus,
                                EKK_PROPOSAL_MODE_CHANGE,
                                proposal_data,
                                threshold,
                                &ballot_id);
    if (err != EKK_OK) {
        printf("ERROR: propose failed: %d\n", err);
        return;
    }
    printf("Ballot ID: %u\n", ballot_id);

    /* Simulate proposal broadcast to all modules */
    for (int i = 1; i < NUM_MODULES; i++) {
        ekk_consensus_on_proposal(&g_modules[i].consensus,
                                  g_modules[0].id,
                                  ballot_id,
                                  EKK_PROPOSAL_MODE_CHANGE,
                                  proposal_data,
                                  threshold);
    }

    /* Each module decides and votes */
    printf("\nVoting:\n");
    for (int i = 0; i < NUM_MODULES; i++) {
        ekk_vote_value_t vote = decide_vote(i, NULL);
        printf("  Module %u votes: %s\n", g_modules[i].id, vote_name(vote));

        /* Send vote to proposer (module 0) */
        ekk_consensus_on_vote(&g_modules[0].consensus,
                              g_modules[i].id,
                              ballot_id,
                              vote);
    }

    /* Tick consensus to finalize */
    now += EKK_VOTE_TIMEOUT_US + 1000;  /* Advance past timeout */
    ekk_consensus_tick(&g_modules[0].consensus, now);

    /* Get result */
    ekk_vote_result_t result = ekk_consensus_get_result(&g_modules[0].consensus, ballot_id);

    printf("\nResult: %s\n", result_name(result));

    /* Count votes */
    int yes_count = 0, no_count = 0;
    for (int i = 0; i < NUM_MODULES; i++) {
        ekk_vote_value_t vote = decide_vote(i, NULL);
        if (vote == EKK_VOTE_YES) yes_count++;
        else if (vote == EKK_VOTE_NO) no_count++;
    }
    printf("Tally: %d YES, %d NO (need %.0f%% = %d votes)\n",
           yes_count, no_count,
           EKK_FIXED_TO_FLOAT(threshold) * 100.0f,
           (int)(NUM_MODULES * EKK_FIXED_TO_FLOAT(threshold) + 0.5f));
}

/**
 * @brief Demonstrate competing proposals and inhibition
 */
static void demo_inhibition(void)
{
    ekk_error_t err;
    ekk_ballot_id_t ballot1, ballot2;
    ekk_time_us_t now = ekk_hal_time_us();

    printf("\n\n=== Inhibition Demo ===\n");
    printf("Two modules propose conflicting modes simultaneously.\n");

    /* Module 1 proposes MODE_A */
    printf("\nModule 1 proposes MODE_A...\n");
    err = ekk_consensus_propose(&g_modules[0].consensus,
                                EKK_PROPOSAL_MODE_CHANGE,
                                1,  /* MODE_A */
                                EKK_THRESHOLD_SUPERMAJORITY,
                                &ballot1);
    if (err != EKK_OK) {
        printf("ERROR: Module 1 propose failed: %d\n", err);
        return;
    }
    printf("Ballot 1 ID: %u\n", ballot1);

    /* Module 2 proposes MODE_B (competing) */
    printf("Module 2 proposes MODE_B (competing)...\n");
    err = ekk_consensus_propose(&g_modules[1].consensus,
                                EKK_PROPOSAL_MODE_CHANGE,
                                2,  /* MODE_B */
                                EKK_THRESHOLD_SUPERMAJORITY,
                                &ballot2);
    if (err != EKK_OK) {
        printf("ERROR: Module 2 propose failed: %d\n", err);
        return;
    }
    printf("Ballot 2 ID: %u\n", ballot2);

    /* Module 3 inhibits the second ballot (prefers MODE_A) */
    printf("\nModule 3 inhibits Ballot 2 (prefers MODE_A)...\n");
    ekk_consensus_inhibit(&g_modules[2].consensus, ballot2);

    /* Simulate some votes for Ballot 1 */
    printf("Voting on Ballot 1 (MODE_A):\n");
    ekk_consensus_on_vote(&g_modules[0].consensus, g_modules[0].id, ballot1, EKK_VOTE_YES);
    ekk_consensus_on_vote(&g_modules[0].consensus, g_modules[1].id, ballot1, EKK_VOTE_NO);
    ekk_consensus_on_vote(&g_modules[0].consensus, g_modules[2].id, ballot1, EKK_VOTE_YES);
    ekk_consensus_on_vote(&g_modules[0].consensus, g_modules[3].id, ballot1, EKK_VOTE_YES);
    ekk_consensus_on_vote(&g_modules[0].consensus, g_modules[4].id, ballot1, EKK_VOTE_YES);
    printf("  4 YES, 1 NO\n");

    /* Finalize */
    now += EKK_VOTE_TIMEOUT_US + 1000;
    ekk_consensus_tick(&g_modules[0].consensus, now);
    ekk_consensus_tick(&g_modules[1].consensus, now);

    ekk_vote_result_t result1 = ekk_consensus_get_result(&g_modules[0].consensus, ballot1);
    ekk_vote_result_t result2 = ekk_consensus_get_result(&g_modules[1].consensus, ballot2);

    printf("\nResults:\n");
    printf("  Ballot 1 (MODE_A): %s\n", result_name(result1));
    printf("  Ballot 2 (MODE_B): %s (inhibited by Module 3)\n", result_name(result2));
}

int main(void)
{
    ekk_error_t err;

    printf("\n");
    printf("*********************************************\n");
    printf("*  EK-KOR v2: Consensus Example             *\n");
    printf("*  Distributed Voting Demo                  *\n");
    printf("*  Copyright (c) 2026 Elektrokombinacija    *\n");
    printf("*********************************************\n");
    printf("\n");

    /* Initialize random seed */
    srand(42);

    /* 1. Initialize EK-KOR system */
    printf("[1] Initializing EK-KOR system...\n");
    err = ekk_init();
    if (err != EKK_OK) {
        fprintf(stderr, "ERROR: ekk_init() failed: %d\n", err);
        return 1;
    }

    /* 2. Initialize modules */
    printf("[2] Initializing %d modules...\n", NUM_MODULES);
    for (int i = 0; i < NUM_MODULES; i++) {
        snprintf(g_module_names[i], sizeof(g_module_names[i]), "VOTER_%d", i + 1);
        ekk_position_t pos = {.x = (int16_t)i, .y = 0, .z = 0};

        err = ekk_module_init(&g_modules[i], (ekk_module_id_t)(i + 1),
                              g_module_names[i], pos);
        if (err != EKK_OK) {
            fprintf(stderr, "ERROR: ekk_module_init(%d) failed: %d\n", i + 1, err);
            return 1;
        }

        /* Start module */
        ekk_module_start(&g_modules[i]);
    }

    /* 3. Setup topology */
    printf("[3] Setting up topology...\n");
    setup_topology();
    printf("    All modules are neighbors\n");

    /* 4. Print voting strategies */
    printf("\n[4] Voting Strategies:\n");
    printf("    Module 1: ALWAYS_YES (proposer)\n");
    printf("    Module 2: ALWAYS_YES\n");
    printf("    Module 3: ALWAYS_YES\n");
    printf("    Module 4: ALWAYS_NO (dissenter)\n");
    printf("    Module 5: FOLLOW_LOAD (conditional)\n");

    /* 5. Run voting rounds */
    printf("\n[5] Running %d voting rounds...\n", VOTE_ROUNDS);

    /* Round 1: Should pass (3 yes + 1 no + module 5 votes no due to low load = 3/5 = 60% < 67%) */
    /* Wait, let me recalculate: 3 yes from modules 1,2,3; 1 no from module 4; module 5 with low load = no
       So 3 yes, 2 no = 60% -- should REJECT */
    run_voting_round(1, 100, EKK_THRESHOLD_SUPERMAJORITY);

    /* Round 2: Module 5 has high load, will vote yes
       4 yes (1,2,3,5), 1 no (4) = 80% -- should APPROVE */
    run_voting_round(2, 200, EKK_THRESHOLD_SUPERMAJORITY);

    /* Round 3: Test simple majority threshold
       3 yes, 2 no = 60% -- should APPROVE with 50% threshold */
    run_voting_round(3, 300, EKK_THRESHOLD_SIMPLE_MAJORITY);

    /* 6. Demonstrate inhibition */
    demo_inhibition();

    /* Summary */
    printf("\n");
    printf("*********************************************\n");
    printf("*  EXAMPLE COMPLETE                         *\n");
    printf("*  Demonstrated:                            *\n");
    printf("*  - Threshold-based voting (67%%, 50%%)    *\n");
    printf("*  - Conditional voting strategies          *\n");
    printf("*  - Proposal inhibition                    *\n");
    printf("*********************************************\n");

    return 0;
}
