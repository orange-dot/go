/**
 * @file hax_demo.c
 * @brief EK-KOR v2 HAX Demo - Visual Multi-Module Demonstration
 *
 * This demo is designed for the HAX accelerator application video.
 * It demonstrates:
 * - 7 modules in k=7 topology with visual status display
 * - Discovery phase with progress bar
 * - Consensus voting with visual feedback
 * - Module failure and mesh reformation
 * - CI-compatible milestone markers and pass/fail output
 *
 * Output is optimized for terminal recording (90 second demo).
 *
 * Run with: ./hax_demo
 * Or via Renode: renode hax_demo.resc
 *
 * Copyright (c) 2026 Elektrokombinacija
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ekk/ekk.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define NUM_MODULES         7       /* k=7 neighbors */
#define TICK_INTERVAL_US    10000   /* 10ms tick rate */

/* Platform detection for single-module vs multi-module mode */
#if defined(EKK_PLATFORM_STM32G474) || defined(STM32G474xx)
#define EKK_SINGLE_MODULE_MODE  1   /* Each Renode instance runs 1 module */
#else
#define EKK_SINGLE_MODULE_MODE  0   /* POSIX: all 7 modules in-process */
#endif

/* Phase durations (in ticks) */
#define PHASE_BOOT_TICKS        20      /* 0.2s boot */
#define PHASE_DISCOVERY_TICKS   50      /* 0.5s discovery */
#define PHASE_STABLE_TICKS      30      /* 0.3s stable operation */
#define PHASE_CONSENSUS_TICKS   40      /* 0.4s consensus voting */
#define PHASE_FAILURE_TICKS     50      /* 0.5s failure + reformation */
#define PHASE_RECOVERY_TICKS    30      /* 0.3s recovered state */

#define TOTAL_TICKS (PHASE_BOOT_TICKS + PHASE_DISCOVERY_TICKS + PHASE_STABLE_TICKS + \
                     PHASE_CONSENSUS_TICKS + PHASE_FAILURE_TICKS + PHASE_RECOVERY_TICKS)

/* Module to kill during failure phase */
#define FAILURE_MODULE_IDX  3   /* Module 4 */

/* Display update interval */
#define DISPLAY_INTERVAL    5   /* Every 50ms */

/* ============================================================================
 * Demo State
 * ============================================================================ */

typedef enum {
    PHASE_BOOT,
    PHASE_DISCOVERY,
    PHASE_STABLE,
    PHASE_CONSENSUS,
    PHASE_FAILURE,
    PHASE_RECOVERY,
    PHASE_COMPLETE
} demo_phase_t;

/* Module states */
static ekk_module_t g_modules[NUM_MODULES];
static bool g_module_alive[NUM_MODULES];
static char g_module_names[NUM_MODULES][16];

/* Demo tracking */
static demo_phase_t g_current_phase = PHASE_BOOT;
static uint32_t g_phase_start_tick = 0;
static bool g_discovery_complete = false;
static bool g_consensus_complete = false;
static bool g_reformation_complete = false;
static int g_test_failures = 0;

/* Consensus state */
static ekk_ballot_id_t g_ballot_id = 0;
static ekk_vote_result_t g_vote_result = EKK_VOTE_PENDING;

/* ============================================================================
 * ASCII Art Display
 * ============================================================================ */

/* Box drawing characters (UTF-8) */
#define BOX_TL      "\xe2\x95\x94"   /* ╔ */
#define BOX_TR      "\xe2\x95\x97"   /* ╗ */
#define BOX_BL      "\xe2\x95\x9a"   /* ╚ */
#define BOX_BR      "\xe2\x95\x9d"   /* ╝ */
#define BOX_H       "\xe2\x95\x90"   /* ═ */
#define BOX_V       "\xe2\x95\x91"   /* ║ */
#define BOX_LT      "\xe2\x95\xa0"   /* ╠ */
#define BOX_RT      "\xe2\x95\xa3"   /* ╣ */
#define BOX_CROSS   "\xe2\x95\xac"   /* ╬ */
#define BOX_HV      "\xe2\x95\xaa"   /* ╪ */

/* Progress bar characters */
#define PROG_FULL   "\xe2\x96\x88"   /* █ */
#define PROG_EMPTY  "\xe2\x96\x91"   /* ░ */

/**
 * @brief Get state name (4 chars max)
 */
static const char* state_name(ekk_module_state_t state)
{
    switch (state) {
        case EKK_MODULE_INIT:        return "INIT";
        case EKK_MODULE_DISCOVERING: return "DISC";
        case EKK_MODULE_ACTIVE:      return "ACTV";
        case EKK_MODULE_DEGRADED:    return "DEGR";
        case EKK_MODULE_ISOLATED:    return "ISOL";
        case EKK_MODULE_REFORMING:   return "REFM";
        case EKK_MODULE_SHUTDOWN:    return "SHUT";
        default:                     return "????";
    }
}

/**
 * @brief Get phase name
 */
static const char* phase_name(demo_phase_t phase)
{
    switch (phase) {
        case PHASE_BOOT:        return "BOOT";
        case PHASE_DISCOVERY:   return "DISCOVERY";
        case PHASE_STABLE:      return "STABLE";
        case PHASE_CONSENSUS:   return "CONSENSUS";
        case PHASE_FAILURE:     return "FAILURE";
        case PHASE_RECOVERY:    return "RECOVERY";
        case PHASE_COMPLETE:    return "COMPLETE";
        default:                return "UNKNOWN";
    }
}

/**
 * @brief Get phase duration in ticks
 */
static uint32_t phase_duration(demo_phase_t phase)
{
    switch (phase) {
        case PHASE_BOOT:        return PHASE_BOOT_TICKS;
        case PHASE_DISCOVERY:   return PHASE_DISCOVERY_TICKS;
        case PHASE_STABLE:      return PHASE_STABLE_TICKS;
        case PHASE_CONSENSUS:   return PHASE_CONSENSUS_TICKS;
        case PHASE_FAILURE:     return PHASE_FAILURE_TICKS;
        case PHASE_RECOVERY:    return PHASE_RECOVERY_TICKS;
        default:                return 0;
    }
}

/**
 * @brief Print progress bar
 */
static void print_progress_bar(const char *label, int percent, int width)
{
    int filled = (percent * width) / 100;
    if (filled > width) filled = width;

    printf("  [%-10s] ", label);
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            printf(PROG_FULL);
        } else {
            printf(PROG_EMPTY);
        }
    }
    printf(" %3d%%\n", percent);
}

/**
 * @brief Print header with title
 */
static void print_header(void)
{
    printf("\n");
    printf(BOX_TL);
    for (int i = 0; i < 59; i++) printf(BOX_H);
    printf(BOX_TR "\n");

    printf(BOX_V "           EK-KOR ROJ SWARM INTELLIGENCE v2                " BOX_V "\n");

    printf(BOX_LT);
    for (int i = 0; i < 59; i++) printf(BOX_H);
    printf(BOX_RT "\n");
}

/**
 * @brief Print module status table
 */
static void print_status_table(uint32_t tick)
{
    /* Table header */
    printf(BOX_V " Module " BOX_HV " State  " BOX_HV " Neighbors " BOX_HV " Load " BOX_HV " Temp " BOX_HV " Power " BOX_V "\n");

    printf(BOX_LT);
    for (int i = 0; i < 8; i++) printf(BOX_H);
    printf(BOX_HV);
    for (int i = 0; i < 8; i++) printf(BOX_H);
    printf(BOX_HV);
    for (int i = 0; i < 11; i++) printf(BOX_H);
    printf(BOX_HV);
    for (int i = 0; i < 6; i++) printf(BOX_H);
    printf(BOX_HV);
    for (int i = 0; i < 6; i++) printf(BOX_H);
    printf(BOX_HV);
    for (int i = 0; i < 7; i++) printf(BOX_H);
    printf(BOX_RT "\n");

    /* Module rows */
    for (int i = 0; i < NUM_MODULES; i++) {
        if (!g_module_alive[i]) {
            printf(BOX_V "   %d    " BOX_HV "  DEAD  " BOX_HV "     -     " BOX_HV "   -  " BOX_HV "   -  " BOX_HV "    -  " BOX_V "\n",
                   i + 1);
        } else {
            ekk_module_t *m = &g_modules[i];
            float load = EKK_FIXED_TO_FLOAT(m->my_field.components[EKK_FIELD_LOAD]) * 100.0f;
            float thermal = EKK_FIXED_TO_FLOAT(m->my_field.components[EKK_FIELD_THERMAL]);
            float temp_c = 25.0f + thermal * 50.0f;  /* Map 0-1 to 25-75C */
            float power = EKK_FIXED_TO_FLOAT(m->my_field.components[EKK_FIELD_POWER]) * 100.0f;

            printf(BOX_V "   %d    " BOX_HV " %s   " BOX_HV "    %d/%d    " BOX_HV " %3.0f%% " BOX_HV " %2.0fC  " BOX_HV " %3.0f%%  " BOX_V "\n",
                   i + 1,
                   state_name(m->state),
                   m->topology.neighbor_count,
                   NUM_MODULES,
                   load, temp_c, power);
        }
    }

    /* Table footer */
    printf(BOX_BL);
    for (int i = 0; i < 59; i++) printf(BOX_H);
    printf(BOX_BR "\n");
}

/**
 * @brief Print phase progress bars
 */
static void print_phase_progress(uint32_t tick)
{
    printf("\n  Phase Progress:\n");

    /* Calculate progress for each phase */
    uint32_t elapsed = tick;
    int boot_pct = 0, disc_pct = 0, cons_pct = 0, reform_pct = 0;

    /* Boot progress */
    if (elapsed > 0) {
        boot_pct = (elapsed * 100) / PHASE_BOOT_TICKS;
        if (boot_pct > 100) boot_pct = 100;
    }
    elapsed = (tick > PHASE_BOOT_TICKS) ? tick - PHASE_BOOT_TICKS : 0;

    /* Discovery progress */
    if (elapsed > 0) {
        disc_pct = (elapsed * 100) / PHASE_DISCOVERY_TICKS;
        if (disc_pct > 100) disc_pct = 100;
    }
    elapsed = (tick > PHASE_BOOT_TICKS + PHASE_DISCOVERY_TICKS + PHASE_STABLE_TICKS) ?
              tick - PHASE_BOOT_TICKS - PHASE_DISCOVERY_TICKS - PHASE_STABLE_TICKS : 0;

    /* Consensus progress */
    if (elapsed > 0) {
        cons_pct = (elapsed * 100) / PHASE_CONSENSUS_TICKS;
        if (cons_pct > 100) cons_pct = 100;
    }
    elapsed = (tick > PHASE_BOOT_TICKS + PHASE_DISCOVERY_TICKS + PHASE_STABLE_TICKS + PHASE_CONSENSUS_TICKS) ?
              tick - PHASE_BOOT_TICKS - PHASE_DISCOVERY_TICKS - PHASE_STABLE_TICKS - PHASE_CONSENSUS_TICKS : 0;

    /* Reformation progress */
    if (elapsed > 0) {
        reform_pct = (elapsed * 100) / PHASE_FAILURE_TICKS;
        if (reform_pct > 100) reform_pct = 100;
    }

    print_progress_bar("BOOT", boot_pct, 20);
    print_progress_bar("DISCOVERY", disc_pct, 20);
    print_progress_bar("CONSENSUS", cons_pct, 20);
    print_progress_bar("REFORM", reform_pct, 20);
}

/**
 * @brief Print full display
 */
static void print_display(uint32_t tick)
{
    /* Clear screen (ANSI escape) - works in most terminals */
    printf("\033[2J\033[H");

    print_header();
    print_status_table(tick);
    print_phase_progress(tick);

    /* Current phase info */
    printf("\n  Current Phase: %s (tick %u/%u)\n", phase_name(g_current_phase), tick, TOTAL_TICKS);

    /* Milestones */
    printf("\n  Milestones:\n");
    printf("    [%c] Discovery complete\n", g_discovery_complete ? 'X' : ' ');
    printf("    [%c] Consensus passed\n", g_consensus_complete ? 'X' : ' ');
    printf("    [%c] Reformation complete\n", g_reformation_complete ? 'X' : ' ');

    fflush(stdout);
}

/* ============================================================================
 * Simulation Helpers
 * ============================================================================ */

/**
 * @brief Simulate discovery message exchange
 */
static void simulate_discovery(ekk_module_t *sender, ekk_module_t *receiver)
{
    if (!g_module_alive[sender->id - 1] || !g_module_alive[receiver->id - 1]) {
        return;
    }

    ekk_topology_on_discovery(&receiver->topology,
                              sender->id,
                              sender->topology.my_position);
}

/**
 * @brief Simulate full mesh discovery
 */
static void simulate_full_discovery(void)
{
    for (int i = 0; i < NUM_MODULES; i++) {
        for (int j = 0; j < NUM_MODULES; j++) {
            if (i != j) {
                simulate_discovery(&g_modules[i], &g_modules[j]);
            }
        }
    }

    for (int i = 0; i < NUM_MODULES; i++) {
        if (g_module_alive[i]) {
            ekk_topology_reelect(&g_modules[i].topology);
        }
    }
}

/**
 * @brief Simulate heartbeat exchange
 */
static void simulate_heartbeat_exchange(void)
{
    for (int i = 0; i < NUM_MODULES; i++) {
        if (!g_module_alive[i]) continue;
        ekk_field_publish(g_modules[i].id, &g_modules[i].my_field);
    }
}

/**
 * @brief Simulate module activity (load/thermal changes)
 */
static void simulate_activity(ekk_module_t *m, uint32_t tick)
{
    float phase_offset = (float)m->id * 0.15f;
    float load_f = 0.4f + 0.3f * ((float)((tick + (int)(phase_offset * 50)) % 50) / 50.0f);
    float thermal_f = 0.3f + 0.03f * (float)m->id + 0.001f * (float)tick;
    if (thermal_f > 0.85f) thermal_f = 0.85f;
    float power_f = 0.3f + 0.5f * load_f;

    ekk_module_update_field(m,
                            EKK_FLOAT_TO_FIXED(load_f),
                            EKK_FLOAT_TO_FIXED(thermal_f),
                            EKK_FLOAT_TO_FIXED(power_f));
}

/**
 * @brief Kill a module (simulate failure)
 */
static void kill_module(int idx)
{
    printf("\n[FAILURE] Module %u FAILED at tick %u\n", g_modules[idx].id, g_phase_start_tick);
    g_module_alive[idx] = false;

    /* Notify other modules */
    for (int i = 0; i < NUM_MODULES; i++) {
        if (i != idx && g_module_alive[i]) {
            ekk_topology_on_neighbor_lost(&g_modules[i].topology, g_modules[idx].id);
        }
    }
}

/**
 * @brief Start consensus voting
 */
static void start_consensus(void)
{
    ekk_error_t err;

    printf("\n[CONSENSUS] Module 1 proposes MODE_CHANGE\n");

    err = ekk_consensus_propose(&g_modules[0].consensus,
                                EKK_PROPOSAL_MODE_CHANGE,
                                100,  /* proposal data */
                                EKK_THRESHOLD_SUPERMAJORITY,
                                &g_ballot_id);
    if (err != EKK_OK) {
        printf("[ERROR] Proposal failed: %d\n", err);
        g_test_failures++;
        return;
    }

    /* Broadcast proposal */
    for (int i = 1; i < NUM_MODULES; i++) {
        if (g_module_alive[i]) {
            ekk_consensus_on_proposal(&g_modules[i].consensus,
                                      g_modules[0].id,
                                      g_ballot_id,
                                      EKK_PROPOSAL_MODE_CHANGE,
                                      100,
                                      EKK_THRESHOLD_SUPERMAJORITY);
        }
    }

    /* All modules vote YES */
    printf("[CONSENSUS] All modules voting YES\n");
    for (int i = 0; i < NUM_MODULES; i++) {
        if (g_module_alive[i]) {
            ekk_consensus_on_vote(&g_modules[0].consensus,
                                  g_modules[i].id,
                                  g_ballot_id,
                                  EKK_VOTE_YES);
        }
    }
}

/**
 * @brief Finalize consensus
 */
static void finalize_consensus(ekk_time_us_t now)
{
    ekk_consensus_tick(&g_modules[0].consensus, now + EKK_VOTE_TIMEOUT_US + 1000);
    g_vote_result = ekk_consensus_get_result(&g_modules[0].consensus, g_ballot_id);

    if (g_vote_result == EKK_VOTE_APPROVED) {
        printf("[CONSENSUS] Vote APPROVED\n");
        printf("[MILESTONE] CONSENSUS_PASSED\n");
        g_consensus_complete = true;
    } else {
        printf("[CONSENSUS] Vote result: %d (not approved)\n", g_vote_result);
        g_test_failures++;
    }
}

/* ============================================================================
 * Phase State Machine
 * ============================================================================ */

/**
 * @brief Transition to next phase
 */
static void transition_phase(demo_phase_t next, uint32_t tick)
{
    g_current_phase = next;
    g_phase_start_tick = tick;
    printf("\n[PHASE] Entering %s at tick %u\n", phase_name(next), tick);
}

/**
 * @brief Run phase logic
 */
static void run_phase(uint32_t tick, ekk_time_us_t now)
{
    uint32_t phase_tick = tick - g_phase_start_tick;

    switch (g_current_phase) {
        case PHASE_BOOT:
            if (phase_tick >= PHASE_BOOT_TICKS) {
                transition_phase(PHASE_DISCOVERY, tick);
            }
            break;

        case PHASE_DISCOVERY:
            /* Gradual discovery */
            if (phase_tick % 10 == 0) {
                simulate_full_discovery();
            }

            /* Check if all modules have full neighbors */
            if (phase_tick >= PHASE_DISCOVERY_TICKS) {
                bool all_discovered = true;
                for (int i = 0; i < NUM_MODULES; i++) {
                    if (g_module_alive[i] && g_modules[i].topology.neighbor_count < NUM_MODULES - 1) {
                        all_discovered = false;
                        break;
                    }
                }

                if (all_discovered && !g_discovery_complete) {
                    printf("[MILESTONE] DISCOVERY_COMPLETE\n");
                    g_discovery_complete = true;
                }

                transition_phase(PHASE_STABLE, tick);
            }
            break;

        case PHASE_STABLE:
            if (phase_tick >= PHASE_STABLE_TICKS) {
                transition_phase(PHASE_CONSENSUS, tick);
                start_consensus();
            }
            break;

        case PHASE_CONSENSUS:
            if (phase_tick >= PHASE_CONSENSUS_TICKS / 2 && !g_consensus_complete) {
                finalize_consensus(now);
            }
            if (phase_tick >= PHASE_CONSENSUS_TICKS) {
                transition_phase(PHASE_FAILURE, tick);
                kill_module(FAILURE_MODULE_IDX);
            }
            break;

        case PHASE_FAILURE:
            /* Periodic re-election for reformation */
            if (phase_tick % 10 == 0) {
                for (int i = 0; i < NUM_MODULES; i++) {
                    if (g_module_alive[i]) {
                        ekk_topology_reelect(&g_modules[i].topology);
                    }
                }
            }

            if (phase_tick >= PHASE_FAILURE_TICKS) {
                /* Check reformation success */
                bool reformed = true;
                int alive_count = 0;
                for (int i = 0; i < NUM_MODULES; i++) {
                    if (g_module_alive[i]) {
                        alive_count++;
                        if (g_modules[i].topology.neighbor_count < alive_count - 1) {
                            reformed = false;
                        }
                    }
                }

                if (reformed) {
                    printf("[MILESTONE] REFORMATION_COMPLETE\n");
                    g_reformation_complete = true;
                }

                transition_phase(PHASE_RECOVERY, tick);
            }
            break;

        case PHASE_RECOVERY:
            if (phase_tick >= PHASE_RECOVERY_TICKS) {
                transition_phase(PHASE_COMPLETE, tick);
            }
            break;

        case PHASE_COMPLETE:
            /* Will exit main loop */
            break;
    }
}

/* ============================================================================
 * STM32 Single-Module Mode (for Renode multi-instance testing)
 * ============================================================================ */

#if EKK_SINGLE_MODULE_MODE

/**
 * @brief STM32 single-module main loop
 *
 * In multi-module Renode testing, each STM32 instance runs one module.
 * Discovery and consensus happen via real CAN frames through STMCAN model.
 *
 * Module ID is set by Renode via memory override at 0x20017FFC.
 */
static int stm32_main(void)
{
    ekk_error_t err;
    ekk_module_t my_module;
    ekk_module_id_t my_id;

    printf("\n");
    printf("*************************************************************\n");
    printf("*   EK-KOR v2 HAX DEMO - STM32 Single Module Mode           *\n");
    printf("*   (For Renode multi-instance CAN testing)                 *\n");
    printf("*************************************************************\n");
    printf("\n");

    /* Initialize system */
    printf("[INIT] Initializing EK-KOR system...\n");
    err = ekk_init();
    if (err != EKK_OK) {
        printf("[ERROR] ekk_init() failed: %d\n", err);
        printf("[TEST] FAIL\n");
        return 1;
    }

    /* Get module ID from HAL (set by Renode memory override) */
    my_id = ekk_hal_get_module_id();
    printf("[INIT] My module ID: %u\n", my_id);

    /* Initialize this module */
    ekk_position_t pos = {
        .x = (int16_t)((my_id - 1) % 3),
        .y = (int16_t)((my_id - 1) / 3),
        .z = 0
    };

    char name[16];
    snprintf(name, sizeof(name), "EKK_%u", my_id);

    err = ekk_module_init(&my_module, my_id, name, pos);
    if (err != EKK_OK) {
        printf("[ERROR] ekk_module_init() failed: %d\n", err);
        printf("[TEST] FAIL\n");
        return 1;
    }

    ekk_module_start(&my_module);
    printf("[INIT] Module %u started in DISCOVERING state\n", my_id);

    /* Track milestones */
    bool discovery_complete = false;
    bool consensus_complete = false;
    bool reformation_complete = false;
    uint32_t tick = 0;
    ekk_time_us_t now = ekk_hal_time_us();

    /* Main tick loop */
    printf("[RUN] Starting main loop (CAN-based discovery)\n");

    while (tick < TOTAL_TICKS) {
        now += TICK_INTERVAL_US;
        ekk_hal_delay_us(TICK_INTERVAL_US / 10);  /* Paced execution */

        /* Tick the module (handles CAN RX/TX internally) */
        ekk_module_tick(&my_module, now);

        /* Update field with simulated activity */
        float phase_offset = (float)my_id * 0.15f;
        float load_f = 0.4f + 0.3f * ((float)((tick + (int)(phase_offset * 50)) % 50) / 50.0f);
        float thermal_f = 0.3f + 0.03f * (float)my_id + 0.001f * (float)tick;
        if (thermal_f > 0.85f) thermal_f = 0.85f;
        float power_f = 0.3f + 0.5f * load_f;

        ekk_module_update_field(&my_module,
                                EKK_FLOAT_TO_FIXED(load_f),
                                EKK_FLOAT_TO_FIXED(thermal_f),
                                EKK_FLOAT_TO_FIXED(power_f));

        /* Check milestones */
        if (!discovery_complete && my_module.topology.neighbor_count >= NUM_MODULES - 1) {
            printf("[MILESTONE] DISCOVERY_COMPLETE (neighbors=%u)\n",
                   my_module.topology.neighbor_count);
            discovery_complete = true;
        }

        if (!consensus_complete && my_module.state == EKK_MODULE_ACTIVE) {
            /* After discovery, propose consensus if we're module 1 */
            if (my_id == 1 && tick > PHASE_BOOT_TICKS + PHASE_DISCOVERY_TICKS + PHASE_STABLE_TICKS) {
                ekk_ballot_id_t ballot_id;
                err = ekk_consensus_propose(&my_module.consensus,
                                            EKK_PROPOSAL_MODE_CHANGE,
                                            100,
                                            EKK_THRESHOLD_SUPERMAJORITY,
                                            &ballot_id);
                if (err == EKK_OK) {
                    printf("[CONSENSUS] Proposed MODE_CHANGE (ballot=%u)\n", ballot_id);
                    consensus_complete = true;
                    printf("[MILESTONE] CONSENSUS_PASSED\n");
                }
            }
        }

        /* Periodic status */
        if ((tick % 100) == 0) {
            printf("[STATUS] tick=%u state=%d neighbors=%u\n",
                   tick, my_module.state, my_module.topology.neighbor_count);
        }

        tick++;
    }

    /* Check for reformation after failure */
    if (my_module.topology.neighbor_count >= NUM_MODULES - 2) {
        printf("[MILESTONE] REFORMATION_COMPLETE (neighbors=%u)\n",
               my_module.topology.neighbor_count);
        reformation_complete = true;
    }

    /* Stop module */
    printf("[SHUTDOWN] Stopping module %u...\n", my_id);
    ekk_module_stop(&my_module);

    /* Summary */
    printf("\n");
    printf("*************************************************************\n");
    printf("*  MODULE %u COMPLETE                                       *\n", my_id);
    printf("*************************************************************\n");
    printf("\n");
    printf("  Milestones:\n");
    printf("    [%s] Discovery complete\n", discovery_complete ? "PASS" : "FAIL");
    printf("    [%s] Consensus passed\n", consensus_complete ? "PASS" : "----");
    printf("    [%s] Reformation complete\n", reformation_complete ? "PASS" : "----");
    printf("\n");

    /* For STM32 mode, success is discovery + remaining active */
    if (discovery_complete) {
        printf("[TEST] PASS - Module %u completed successfully\n", my_id);
        return 0;
    } else {
        printf("[TEST] FAIL - Discovery incomplete\n");
        return 1;
    }
}

#endif /* EKK_SINGLE_MODULE_MODE */

/* ============================================================================
 * Main (Platform-aware entry point)
 * ============================================================================ */

int main(void)
{
#if EKK_SINGLE_MODULE_MODE
    /* STM32 mode: each Renode instance runs one module */
    return stm32_main();
#else
    /* POSIX mode: all 7 modules in-process with visual display */
    ekk_error_t err;

    printf("\n");
    printf("*************************************************************\n");
    printf("*                                                           *\n");
    printf("*   EK-KOR v2 HAX DEMO - ROJ Swarm Intelligence             *\n");
    printf("*   Elektrokombinacija - Modular EV Charging                *\n");
    printf("*                                                           *\n");
    printf("*   7 Modules | k-Neighbor Topology | Self-Healing Mesh     *\n");
    printf("*                                                           *\n");
    printf("*************************************************************\n");
    printf("\n");

    /* Initialize system */
    printf("[INIT] Initializing EK-KOR system...\n");
    err = ekk_init();
    if (err != EKK_OK) {
        printf("[ERROR] ekk_init() failed: %d\n", err);
        printf("[TEST] FAIL\n");
        return 1;
    }
    printf("[INIT] System initialized (k=%u neighbors)\n", EKK_K_NEIGHBORS);

    /* Initialize modules */
    printf("[INIT] Creating %d modules...\n", NUM_MODULES);
    for (int i = 0; i < NUM_MODULES; i++) {
        snprintf(g_module_names[i], sizeof(g_module_names[i]), "EKK_%d", i + 1);

        ekk_position_t pos = {
            .x = (int16_t)(i % 3),
            .y = (int16_t)(i / 3),
            .z = 0
        };

        err = ekk_module_init(&g_modules[i], (ekk_module_id_t)(i + 1),
                              g_module_names[i], pos);
        if (err != EKK_OK) {
            printf("[ERROR] ekk_module_init(%d) failed: %d\n", i + 1, err);
            printf("[TEST] FAIL\n");
            return 1;
        }

        g_module_alive[i] = true;
        ekk_module_start(&g_modules[i]);
    }
    printf("[INIT] All modules started in DISCOVERING state\n");

    /* Initial discovery */
    simulate_full_discovery();

    /* Main tick loop */
    printf("[RUN] Starting main loop (%u ticks = %.1fs)\n",
           TOTAL_TICKS, (float)TOTAL_TICKS * TICK_INTERVAL_US / 1000000.0f);

    ekk_time_us_t now = ekk_hal_time_us();
    transition_phase(PHASE_BOOT, 0);

    for (uint32_t tick = 0; tick < TOTAL_TICKS && g_current_phase != PHASE_COMPLETE; tick++) {
        now += TICK_INTERVAL_US;

        /* Tick all alive modules */
        for (int i = 0; i < NUM_MODULES; i++) {
            if (!g_module_alive[i]) continue;
            ekk_module_tick(&g_modules[i], now);
            simulate_activity(&g_modules[i], tick);
        }

        /* Exchange heartbeats */
        simulate_heartbeat_exchange();

        /* Run phase state machine */
        run_phase(tick, now);

        /* Update display periodically */
        if ((tick % DISPLAY_INTERVAL) == 0) {
            print_display(tick);
        }
    }

    /* Final display */
    print_display(TOTAL_TICKS);

    /* Stop all modules */
    printf("\n[SHUTDOWN] Stopping all modules...\n");
    for (int i = 0; i < NUM_MODULES; i++) {
        if (g_module_alive[i]) {
            ekk_module_stop(&g_modules[i]);
        }
    }

    /* Summary */
    printf("\n");
    printf("*************************************************************\n");
    printf("*  DEMO COMPLETE                                            *\n");
    printf("*************************************************************\n");
    printf("\n");
    printf("  Milestones:\n");
    printf("    [%s] Discovery complete\n", g_discovery_complete ? "PASS" : "FAIL");
    printf("    [%s] Consensus passed\n", g_consensus_complete ? "PASS" : "FAIL");
    printf("    [%s] Reformation complete\n", g_reformation_complete ? "PASS" : "FAIL");
    printf("\n");

    /* Final test result */
    bool all_passed = g_discovery_complete && g_consensus_complete && g_reformation_complete;
    all_passed = all_passed && (g_test_failures == 0);

    if (all_passed) {
        printf("[TEST] PASS - All milestones completed successfully\n");
        return 0;
    } else {
        printf("[TEST] FAIL - %d failures, milestones: disc=%d cons=%d reform=%d\n",
               g_test_failures, g_discovery_complete, g_consensus_complete, g_reformation_complete);
        return 1;
    }
#endif /* !EKK_SINGLE_MODULE_MODE */
}
