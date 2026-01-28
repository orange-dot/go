/**
 * @file renode_test.c
 * @brief EK-KOR v2 Test Application for Renode
 *
 * Simple test that demonstrates:
 * - HAL initialization
 * - Module creation and tick loop
 * - UART output
 *
 * Run with: renode ekk_single.resc
 *
 * Copyright (c) 2026 Elektrokombinacija
 * SPDX-License-Identifier: MIT
 */

#include "ekk/ekk.h"
#include "ekk/ekk_hal.h"
#include "ekk/ekk_field.h"
#include "ekk/ekk_module.h"

/* Test configuration */
#define TEST_TICK_INTERVAL_US   1000    /* 1ms tick rate */
#define TEST_PRINT_INTERVAL_US  1000000 /* Print status every 1s */
#define TEST_DURATION_TICKS     10000   /* Run for 10 seconds */

/* Module state */
static ekk_module_t g_module;
static uint32_t g_tick_count = 0;
static ekk_time_us_t g_last_print = 0;

/**
 * @brief Initialize system clocks to 170 MHz
 */
void SystemInit(void)
{
    /* For Renode emulation, HSI @ 16 MHz works fine */
}

/**
 * @brief Print module status with CAN statistics
 */
static void print_status(void)
{
    ekk_hal_printf("\n=== EK-KOR v2 Status (tick %lu) ===\n", g_tick_count);
    ekk_hal_printf("Module ID: %u\n", g_module.id);
    ekk_hal_printf("State:     %s\n", ekk_module_state_str(g_module.state));
    ekk_hal_printf("Platform:  %s\n", ekk_hal_platform_name());
    ekk_hal_printf("Time:      %lu us\n", (unsigned long)ekk_hal_time_us());

    /* Field values */
    ekk_hal_printf("Field - Load: %ld, Thermal: %ld, Power: %ld\n",
                   (long)g_module.my_field.components[EKK_FIELD_LOAD],
                   (long)g_module.my_field.components[EKK_FIELD_THERMAL],
                   (long)g_module.my_field.components[EKK_FIELD_POWER]);

    /* Topology info - key for multi-module testing */
    ekk_hal_printf("Known: %u, Neighbors: %u (k=%u target)\n",
                   g_module.topology.known_count,
                   g_module.topology.neighbor_count,
                   EKK_K_NEIGHBORS);

    /* List discovered neighbors if any */
    if (g_module.topology.neighbor_count > 0) {
        ekk_hal_printf("Neighbor IDs: ");
        for (uint8_t i = 0; i < g_module.topology.neighbor_count && i < 8; i++) {
            ekk_hal_printf("%u ", g_module.topology.neighbors[i].id);
        }
        ekk_hal_printf("\n");
    }

    ekk_hal_printf("Ticks: %lu, Field updates: %lu\n",
                   g_module.ticks_total, g_module.field_updates);
    ekk_hal_printf("================================\n\n");
}

/**
 * @brief Simulate some module activity
 */
static void simulate_activity(void)
{
    /* Update load based on tick count (simulated workload) */
    ekk_fixed_t load = ((g_tick_count % 100) * EKK_FIXED_ONE) / 100;

    /* Thermal slowly increases */
    ekk_fixed_t thermal = ((g_tick_count % 500) * EKK_FIXED_ONE) / 500;

    /* Power fluctuates */
    ekk_fixed_t power = EKK_FIXED_HALF +
                        ((g_tick_count % 50) * EKK_FIXED_QUARTER) / 50;

    /* Update my field */
    g_module.my_field.components[EKK_FIELD_LOAD] = load;
    g_module.my_field.components[EKK_FIELD_THERMAL] = thermal;
    g_module.my_field.components[EKK_FIELD_POWER] = power;
    g_module.my_field.timestamp = ekk_hal_time_us();

    /* Publish to field region */
    ekk_field_publish(g_module.id, &g_module.my_field);
}

/**
 * @brief Main function
 */
int main(void)
{
    ekk_error_t err;
    ekk_time_us_t now, next_tick;

    /* Initialize HAL (timer, serial, CAN) */
    err = ekk_hal_init();
    if (err != EKK_OK) {
        while (1) __asm__ volatile("wfi");
    }

    /* Print banner */
    ekk_hal_printf("\n");
    ekk_hal_printf("*********************************************\n");
    ekk_hal_printf("*  EK-KOR v2: Field-Centric Coordination    *\n");
    ekk_hal_printf("*  Renode Test Application                  *\n");
    ekk_hal_printf("*  Copyright (c) 2026 Elektrokombinacija    *\n");
    ekk_hal_printf("*********************************************\n");
    ekk_hal_printf("\n");

    /* Initialize EK-KOR system */
    err = ekk_init();
    if (err != EKK_OK) {
        ekk_hal_printf("ERROR: ekk_init() failed: %d\n", err);
        while (1);
    }
    ekk_hal_printf("EKK system initialized\n");

    /* Initialize module */
    ekk_module_id_t my_id = ekk_hal_get_module_id();
    ekk_position_t my_pos = { .x = 0, .y = 0, .z = 0 };

    err = ekk_module_init(&g_module, my_id, "EKK_TEST", my_pos);
    if (err != EKK_OK) {
        ekk_hal_printf("ERROR: ekk_module_init() failed: %d\n", err);
        while (1);
    }
    ekk_hal_printf("Module %u initialized (k=%u)\n", my_id, EKK_K_NEIGHBORS);

    /* Start module */
    err = ekk_module_start(&g_module);
    if (err != EKK_OK) {
        ekk_hal_printf("ERROR: ekk_module_start() failed: %d\n", err);
        while (1);
    }
    ekk_hal_printf("Module started - entering main loop\n\n");

    /* Main loop */
    now = ekk_hal_time_us();
    next_tick = now + TEST_TICK_INTERVAL_US;
    g_last_print = now;

    while (g_tick_count < TEST_DURATION_TICKS) {
        now = ekk_hal_time_us();

        /* Tick at fixed interval */
        if (now >= next_tick) {
            /* Run module tick */
            err = ekk_module_tick(&g_module, now);
            if (err != EKK_OK && err != EKK_ERR_NOT_FOUND) {
                ekk_hal_printf("WARN: tick error %d\n", err);
            }

            /* Simulate activity */
            simulate_activity();

            g_tick_count++;
            next_tick += TEST_TICK_INTERVAL_US;
        }

        /* Print status periodically */
        if (now - g_last_print >= TEST_PRINT_INTERVAL_US) {
            print_status();
            g_last_print = now;
        }

        /* Low-power wait */
        __asm__ volatile("wfi");
    }

    /* Test complete */
    ekk_hal_printf("\n");
    ekk_hal_printf("*********************************************\n");
    ekk_hal_printf("*  TEST COMPLETE: %lu ticks executed        \n", g_tick_count);
    ekk_hal_printf("*********************************************\n");

    /* Stop module */
    ekk_module_stop(&g_module);

    /* Halt */
    while (1) {
        __asm__ volatile("wfi");
    }

    return 0;
}
