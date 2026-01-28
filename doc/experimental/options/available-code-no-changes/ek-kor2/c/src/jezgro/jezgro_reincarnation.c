/**
 * @file jezgro_reincarnation.c
 * @brief JEZGRO Microkernel - Reincarnation Server Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license AGPL-3.0
 */

#include "../../include/ekk/jezgro/jezgro_reincarnation.h"
#include "../../include/ekk/ekk_hal.h"
#include <string.h>

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

/** Managed services */
static jezgro_service_info_t g_services[JEZGRO_RS_MAX_SERVICES];

/** Number of registered services */
static uint8_t g_num_services = 0;

/** RS running flag */
static bool g_rs_running = false;

/** Crash callback */
static jezgro_crash_callback_t g_crash_callback = NULL;

/** Statistics */
static jezgro_rs_stats_t g_stats = {0};

/** Last heartbeat check time */
static uint32_t g_last_heartbeat_check_ms = 0;

/* ============================================================================
 * INTERNAL FUNCTIONS
 * ============================================================================ */

static jezgro_service_info_t* find_service(uint8_t service_id)
{
    for (int i = 0; i < g_num_services; i++) {
        if (g_services[i].id == service_id) {
            return &g_services[i];
        }
    }
    return NULL;
}

static const char* state_to_string(jezgro_service_state_t state)
{
    switch (state) {
        case JEZGRO_SERVICE_STATE_INIT:     return "INIT";
        case JEZGRO_SERVICE_STATE_STARTING: return "STARTING";
        case JEZGRO_SERVICE_STATE_RUNNING:  return "RUNNING";
        case JEZGRO_SERVICE_STATE_STOPPING: return "STOPPING";
        case JEZGRO_SERVICE_STATE_CRASHED:  return "CRASHED";
        case JEZGRO_SERVICE_STATE_DEAD:     return "DEAD";
        default:                            return "UNKNOWN";
    }
}

static const char* crash_to_string(jezgro_crash_reason_t reason)
{
    switch (reason) {
        case JEZGRO_CRASH_NONE:       return "none";
        case JEZGRO_CRASH_MPU_FAULT:  return "MPU fault";
        case JEZGRO_CRASH_HEARTBEAT:  return "heartbeat timeout";
        case JEZGRO_CRASH_EXCEPTION:  return "exception";
        case JEZGRO_CRASH_PANIC:      return "panic";
        case JEZGRO_CRASH_WATCHDOG:   return "watchdog";
        case JEZGRO_CRASH_REQUESTED:  return "requested";
        default:                      return "unknown";
    }
}

/**
 * @brief Actually start a service
 */
static ekk_error_t do_start_service(jezgro_service_info_t* svc, uint32_t now_ms)
{
    uint32_t start_time = ekk_hal_time_us();

    ekk_hal_printf("JEZGRO RS: Starting service '%s' (id=%d)\n",
                   svc->name, svc->id);

    svc->state = JEZGRO_SERVICE_STATE_STARTING;

    /* Register with MPU */
    ekk_error_t err = jezgro_mpu_register_service(&svc->desc);
    if (err != EKK_OK) {
        ekk_hal_printf("JEZGRO RS: Failed to register MPU for '%s'\n", svc->name);
        return err;
    }

    /* Register IPC endpoint */
    jezgro_endpoint_t ep = JEZGRO_EP_ROJ;  /* Default endpoint */
    if (svc->id == 2) ep = JEZGRO_EP_LLC;
    else if (svc->id == 4) ep = JEZGRO_EP_DIAGNOSTICS;

    err = jezgro_ipc_register_endpoint(ep, svc->id);
    if (err != EKK_OK && err != EKK_ERR_INVALID_ARG) {
        /* Ignore already-registered error */
        ekk_hal_printf("JEZGRO RS: Warning - IPC register failed for '%s'\n",
                       svc->name);
    }

    /* Switch MPU context and call entry point */
    jezgro_mpu_switch_context(svc->id);

    /* In a real implementation, this would set up the service stack
     * and jump to entry point. For now, we just mark it as running. */
    if (svc->desc.entry != NULL) {
        /* Would normally call: svc->desc.entry(); */
        /* But we need proper context switching first */
    }

    /* Mark as running */
    svc->state = JEZGRO_SERVICE_STATE_RUNNING;
    svc->start_time_ms = now_ms;
    svc->last_heartbeat_ms = now_ms;
    svc->missed_heartbeats = 0;

    /* Update statistics */
    uint32_t restart_time = ekk_hal_time_us() - start_time;
    svc->last_restart_duration_ms = restart_time / 1000;

    g_stats.services_running++;

    /* Track restart time for metrics */
    if (g_stats.min_restart_time_us == 0 || restart_time < g_stats.min_restart_time_us) {
        g_stats.min_restart_time_us = restart_time;
    }
    if (restart_time > g_stats.max_restart_time_us) {
        g_stats.max_restart_time_us = restart_time;
    }

    /* Rolling average */
    if (g_stats.avg_restart_time_us == 0) {
        g_stats.avg_restart_time_us = restart_time;
    } else {
        g_stats.avg_restart_time_us = (g_stats.avg_restart_time_us * 3 + restart_time) / 4;
    }

    ekk_hal_printf("JEZGRO RS: Service '%s' started in %uus\n",
                   svc->name, restart_time);

    return EKK_OK;
}

/**
 * @brief Actually stop a service
 */
static ekk_error_t do_stop_service(jezgro_service_info_t* svc, bool graceful)
{
    ekk_hal_printf("JEZGRO RS: Stopping service '%s' (graceful=%d)\n",
                   svc->name, graceful);

    if (graceful && svc->state == JEZGRO_SERVICE_STATE_RUNNING) {
        svc->state = JEZGRO_SERVICE_STATE_STOPPING;

        /* Send shutdown message via IPC */
        jezgro_ipc_notify(svc->id, NULL, 0);

        /* Wait for graceful shutdown (would timeout in real impl) */
    }

    /* Calculate uptime */
    uint32_t now = ekk_hal_time_ms();
    if (svc->start_time_ms > 0) {
        svc->total_uptime_ms += now - svc->start_time_ms;
    }

    /* Disable MPU region for this service */
    /* In real impl: jezgro_mpu_disable_service(svc->id); */

    svc->state = JEZGRO_SERVICE_STATE_INIT;
    g_stats.services_running--;

    return EKK_OK;
}

/**
 * @brief Handle service crash and potential restart
 */
static void handle_crash(jezgro_service_info_t* svc,
                          jezgro_crash_reason_t reason,
                          uint32_t now_ms)
{
    ekk_hal_printf("\n*** JEZGRO RS: Service '%s' CRASHED ***\n", svc->name);
    ekk_hal_printf("    Reason: %s\n", crash_to_string(reason));
    ekk_hal_printf("    Restart count: %d/%d\n",
                   svc->consecutive_crashes, JEZGRO_RS_MAX_RESTART_ATTEMPTS);

    /* Record crash */
    svc->state = JEZGRO_SERVICE_STATE_CRASHED;
    svc->last_crash = reason;
    svc->last_crash_time_ms = now_ms;
    svc->consecutive_crashes++;
    svc->restart_count++;

    /* Update stats */
    g_stats.total_restarts++;
    g_stats.services_running--;

    if (reason == JEZGRO_CRASH_MPU_FAULT) {
        g_stats.mpu_faults++;
    } else if (reason == JEZGRO_CRASH_HEARTBEAT) {
        g_stats.heartbeat_timeouts++;
    }

    /* Calculate uptime */
    if (svc->start_time_ms > 0) {
        svc->total_uptime_ms += now_ms - svc->start_time_ms;
    }

    /* Invoke crash callback */
    if (g_crash_callback) {
        g_crash_callback(svc->id, reason);
    }

    /* Check if we should restart */
    if (!svc->auto_restart) {
        ekk_hal_printf("JEZGRO RS: Service '%s' not configured for auto-restart\n",
                       svc->name);
        return;
    }

    if (svc->consecutive_crashes >= JEZGRO_RS_MAX_RESTART_ATTEMPTS) {
        ekk_hal_printf("JEZGRO RS: Service '%s' exceeded max restart attempts, marking DEAD\n",
                       svc->name);
        svc->state = JEZGRO_SERVICE_STATE_DEAD;
        g_stats.services_dead++;

        if (svc->critical) {
            ekk_hal_printf("*** CRITICAL SERVICE DEAD - SYSTEM DEGRADED ***\n");
        }
        return;
    }

    /* Calculate backoff delay */
    uint32_t backoff_ms = JEZGRO_RS_RESTART_BACKOFF_MS * svc->consecutive_crashes;
    ekk_hal_printf("JEZGRO RS: Will restart '%s' in %ums\n", svc->name, backoff_ms);

    /* In a real implementation, we'd schedule the restart.
     * For now, restart immediately if no backoff. */
    if (backoff_ms == 0) {
        do_start_service(svc, now_ms);
    }
}

/**
 * @brief Send heartbeat ping to a service
 */
static void send_heartbeat_ping(jezgro_service_info_t* svc)
{
    /* In real implementation, this would send IPC heartbeat message */
    (void)svc;
}

/**
 * @brief Check heartbeats for all running services
 */
static void check_heartbeats(uint32_t now_ms)
{
    for (int i = 0; i < g_num_services; i++) {
        jezgro_service_info_t* svc = &g_services[i];

        if (svc->state != JEZGRO_SERVICE_STATE_RUNNING) {
            continue;
        }

        uint32_t elapsed = now_ms - svc->last_heartbeat_ms;

        if (elapsed > JEZGRO_RS_HEARTBEAT_TIMEOUT_MS) {
            svc->missed_heartbeats++;

            if (svc->missed_heartbeats >= 3) {
                /* Service is unresponsive */
                handle_crash(svc, JEZGRO_CRASH_HEARTBEAT, now_ms);
            } else {
                /* Send another ping */
                send_heartbeat_ping(svc);
            }
        }
    }
}

/**
 * @brief Handle pending restarts with backoff
 */
static void check_pending_restarts(uint32_t now_ms)
{
    for (int i = 0; i < g_num_services; i++) {
        jezgro_service_info_t* svc = &g_services[i];

        if (svc->state != JEZGRO_SERVICE_STATE_CRASHED) {
            continue;
        }

        uint32_t backoff_ms = JEZGRO_RS_RESTART_BACKOFF_MS * svc->consecutive_crashes;
        uint32_t elapsed = now_ms - svc->last_crash_time_ms;

        if (elapsed >= backoff_ms) {
            ekk_hal_printf("JEZGRO RS: Restarting '%s' after backoff\n", svc->name);
            do_start_service(svc, now_ms);
        }
    }
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

ekk_error_t jezgro_rs_init(void)
{
    memset(g_services, 0, sizeof(g_services));
    memset(&g_stats, 0, sizeof(g_stats));

    g_num_services = 0;
    g_rs_running = false;
    g_crash_callback = NULL;

    ekk_hal_printf("JEZGRO: Reincarnation Server initialized\n");

    return EKK_OK;
}

void jezgro_rs_start(void)
{
    g_rs_running = true;
    g_last_heartbeat_check_ms = ekk_hal_time_ms();

    ekk_hal_printf("JEZGRO: Reincarnation Server started\n");
}

void jezgro_rs_stop(void)
{
    g_rs_running = false;

    /* Stop all services */
    for (int i = 0; i < g_num_services; i++) {
        if (g_services[i].state == JEZGRO_SERVICE_STATE_RUNNING) {
            do_stop_service(&g_services[i], true);
        }
    }

    ekk_hal_printf("JEZGRO: Reincarnation Server stopped\n");
}

ekk_error_t jezgro_rs_register_service(const jezgro_service_desc_t* desc,
                                        bool critical,
                                        bool auto_restart)
{
    if (desc == NULL || g_num_services >= JEZGRO_RS_MAX_SERVICES) {
        return EKK_ERR_LIMIT;
    }

    jezgro_service_info_t* svc = &g_services[g_num_services];

    svc->id = desc->id;
    svc->name = desc->name;
    svc->state = JEZGRO_SERVICE_STATE_INIT;
    memcpy(&svc->desc, desc, sizeof(jezgro_service_desc_t));
    svc->critical = critical;
    svc->auto_restart = auto_restart;

    svc->last_heartbeat_ms = 0;
    svc->missed_heartbeats = 0;
    svc->restart_count = 0;
    svc->consecutive_crashes = 0;
    svc->last_crash = JEZGRO_CRASH_NONE;
    svc->total_uptime_ms = 0;

    g_num_services++;

    ekk_hal_printf("JEZGRO RS: Registered service '%s' (id=%d, critical=%d, auto=%d)\n",
                   desc->name, desc->id, critical, auto_restart);

    return EKK_OK;
}

ekk_error_t jezgro_rs_start_service(uint8_t service_id)
{
    jezgro_service_info_t* svc = find_service(service_id);
    if (svc == NULL) {
        return EKK_ERR_NOT_FOUND;
    }

    if (svc->state == JEZGRO_SERVICE_STATE_RUNNING) {
        return EKK_OK;  /* Already running */
    }

    if (svc->state == JEZGRO_SERVICE_STATE_DEAD) {
        return EKK_ERR_INVALID_ARG;  /* Can't start dead service */
    }

    /* Reset consecutive crashes on manual start */
    svc->consecutive_crashes = 0;

    return do_start_service(svc, ekk_hal_time_ms());
}

ekk_error_t jezgro_rs_stop_service(uint8_t service_id, bool graceful)
{
    jezgro_service_info_t* svc = find_service(service_id);
    if (svc == NULL) {
        return EKK_ERR_NOT_FOUND;
    }

    return do_stop_service(svc, graceful);
}

ekk_error_t jezgro_rs_restart_service(uint8_t service_id)
{
    jezgro_service_info_t* svc = find_service(service_id);
    if (svc == NULL) {
        return EKK_ERR_NOT_FOUND;
    }

    /* Stop if running */
    if (svc->state == JEZGRO_SERVICE_STATE_RUNNING) {
        do_stop_service(svc, false);  /* Force stop */
    }

    /* Mark as crashed to use restart machinery */
    svc->state = JEZGRO_SERVICE_STATE_CRASHED;
    svc->last_crash = JEZGRO_CRASH_REQUESTED;
    svc->last_crash_time_ms = ekk_hal_time_ms();

    /* Don't increment consecutive crashes for requested restart */

    return do_start_service(svc, ekk_hal_time_ms());
}

ekk_error_t jezgro_rs_get_service_info(uint8_t service_id,
                                        jezgro_service_info_t* info)
{
    jezgro_service_info_t* svc = find_service(service_id);
    if (svc == NULL || info == NULL) {
        return EKK_ERR_NOT_FOUND;
    }

    memcpy(info, svc, sizeof(jezgro_service_info_t));
    return EKK_OK;
}

void jezgro_rs_tick(uint32_t now_ms)
{
    if (!g_rs_running) {
        return;
    }

    /* Check heartbeats periodically */
    if (now_ms - g_last_heartbeat_check_ms >= JEZGRO_RS_HEARTBEAT_INTERVAL_MS) {
        check_heartbeats(now_ms);
        g_last_heartbeat_check_ms = now_ms;
    }

    /* Check for pending restarts */
    check_pending_restarts(now_ms);
}

void jezgro_rs_heartbeat(uint8_t service_id)
{
    jezgro_service_info_t* svc = find_service(service_id);
    if (svc == NULL) {
        return;
    }

    svc->last_heartbeat_ms = ekk_hal_time_ms();
    svc->missed_heartbeats = 0;

    /* Reset consecutive crashes after successful heartbeat */
    if (svc->consecutive_crashes > 0) {
        /* Service has been stable, reduce crash counter */
        svc->consecutive_crashes = 0;
    }
}

void jezgro_rs_report_crash(uint8_t service_id, jezgro_crash_reason_t reason)
{
    jezgro_service_info_t* svc = find_service(service_id);
    if (svc == NULL) {
        ekk_hal_printf("JEZGRO RS: Crash reported for unknown service %d\n", service_id);
        return;
    }

    handle_crash(svc, reason, ekk_hal_time_ms());
}

void jezgro_rs_set_crash_callback(jezgro_crash_callback_t callback)
{
    g_crash_callback = callback;
}

void jezgro_rs_get_stats(jezgro_rs_stats_t* stats)
{
    if (stats != NULL) {
        memcpy(stats, &g_stats, sizeof(jezgro_rs_stats_t));
    }
}

void jezgro_rs_reset_stats(void)
{
    uint8_t running = g_stats.services_running;
    uint8_t dead = g_stats.services_dead;

    memset(&g_stats, 0, sizeof(g_stats));

    g_stats.services_running = running;
    g_stats.services_dead = dead;
}

void jezgro_rs_dump_state(void)
{
    ekk_hal_printf("\nJEZGRO Reincarnation Server State:\n");
    ekk_hal_printf("  Running: %s\n", g_rs_running ? "yes" : "no");
    ekk_hal_printf("  Services: %d registered\n", g_num_services);

    ekk_hal_printf("\n  Statistics:\n");
    ekk_hal_printf("    Total restarts: %u\n", g_stats.total_restarts);
    ekk_hal_printf("    MPU faults: %u\n", g_stats.mpu_faults);
    ekk_hal_printf("    Heartbeat timeouts: %u\n", g_stats.heartbeat_timeouts);
    ekk_hal_printf("    Restart time (us): min=%u, avg=%u, max=%u\n",
                   g_stats.min_restart_time_us,
                   g_stats.avg_restart_time_us,
                   g_stats.max_restart_time_us);
    ekk_hal_printf("    Services: %d running, %d dead\n",
                   g_stats.services_running, g_stats.services_dead);

    ekk_hal_printf("\n  Services:\n");
    for (int i = 0; i < g_num_services; i++) {
        jezgro_rs_dump_service(g_services[i].id);
    }
}

void jezgro_rs_dump_service(uint8_t service_id)
{
    jezgro_service_info_t* svc = find_service(service_id);
    if (svc == NULL) {
        ekk_hal_printf("    Service %d: not found\n", service_id);
        return;
    }

    ekk_hal_printf("    [%d] %s:\n", svc->id, svc->name);
    ekk_hal_printf("      State: %s\n", state_to_string(svc->state));
    ekk_hal_printf("      Critical: %s, Auto-restart: %s\n",
                   svc->critical ? "yes" : "no",
                   svc->auto_restart ? "yes" : "no");
    ekk_hal_printf("      Restarts: %d total, %d consecutive\n",
                   svc->restart_count, svc->consecutive_crashes);

    if (svc->last_crash != JEZGRO_CRASH_NONE) {
        ekk_hal_printf("      Last crash: %s @ %ums\n",
                       crash_to_string(svc->last_crash),
                       svc->last_crash_time_ms);
    }

    ekk_hal_printf("      Uptime: %ums\n", svc->total_uptime_ms);

    if (svc->last_restart_duration_ms > 0) {
        ekk_hal_printf("      Last restart: %ums (target: %dms)\n",
                       svc->last_restart_duration_ms,
                       JEZGRO_RS_TARGET_RESTART_MS);
    }
}
