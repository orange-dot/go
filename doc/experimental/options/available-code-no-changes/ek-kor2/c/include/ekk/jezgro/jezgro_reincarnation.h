/**
 * @file jezgro_reincarnation.h
 * @brief JEZGRO Microkernel - Reincarnation Server
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license AGPL-3.0
 *
 * The Reincarnation Server (RS) is a special JEZGRO service responsible for:
 * 1. Monitoring service health via heartbeats
 * 2. Detecting service crashes (MPU faults, timeouts)
 * 3. Automatically restarting failed services
 * 4. Managing service lifecycle (start, stop, restart)
 *
 * DESIGN GOALS:
 * - Service restart time < 50ms (paper target)
 * - No single point of failure (RS itself can be restarted by kernel)
 * - Minimal memory footprint
 * - Support for crash counters and backoff
 *
 * PAPER REFERENCE:
 * ROJ Paper Section IV.B: "The Reincarnation Server monitors service health
 * and automatically restarts failed services within 50ms, ensuring system
 * availability despite individual component failures."
 */

#ifndef JEZGRO_REINCARNATION_H
#define JEZGRO_REINCARNATION_H

#include "../ekk_types.h"
#include "jezgro_mpu.h"
#include "jezgro_ipc.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/** Maximum number of managed services */
#define JEZGRO_RS_MAX_SERVICES      8

/** Heartbeat interval (ms) - services must respond within this */
#define JEZGRO_RS_HEARTBEAT_INTERVAL_MS    20

/** Heartbeat timeout (ms) - service is dead if no response */
#define JEZGRO_RS_HEARTBEAT_TIMEOUT_MS     50

/** Maximum restart attempts before giving up */
#define JEZGRO_RS_MAX_RESTART_ATTEMPTS     5

/** Backoff multiplier for restart delays */
#define JEZGRO_RS_RESTART_BACKOFF_MS       100

/** Target restart time (ms) */
#define JEZGRO_RS_TARGET_RESTART_MS        50

/* ============================================================================
 * SERVICE STATES
 * ============================================================================ */

/**
 * @brief Service lifecycle state
 */
typedef enum {
    JEZGRO_SERVICE_STATE_INIT       = 0,  /**< Not yet started */
    JEZGRO_SERVICE_STATE_STARTING   = 1,  /**< Starting up */
    JEZGRO_SERVICE_STATE_RUNNING    = 2,  /**< Running normally */
    JEZGRO_SERVICE_STATE_STOPPING   = 3,  /**< Graceful shutdown */
    JEZGRO_SERVICE_STATE_CRASHED    = 4,  /**< Crashed, pending restart */
    JEZGRO_SERVICE_STATE_DEAD       = 5,  /**< Permanently dead (max restarts) */
} jezgro_service_state_t;

/**
 * @brief Crash reason
 */
typedef enum {
    JEZGRO_CRASH_NONE           = 0,
    JEZGRO_CRASH_MPU_FAULT      = 1,  /**< Memory protection violation */
    JEZGRO_CRASH_HEARTBEAT      = 2,  /**< Missed heartbeat deadline */
    JEZGRO_CRASH_EXCEPTION      = 3,  /**< Unhandled exception */
    JEZGRO_CRASH_PANIC          = 4,  /**< Service called panic() */
    JEZGRO_CRASH_WATCHDOG       = 5,  /**< Watchdog timeout */
    JEZGRO_CRASH_REQUESTED      = 6,  /**< Restart requested by IPC */
} jezgro_crash_reason_t;

/* ============================================================================
 * SERVICE INFO
 * ============================================================================ */

/**
 * @brief Service management info
 */
typedef struct {
    uint8_t id;                         /**< Service ID */
    const char* name;                   /**< Human-readable name */
    jezgro_service_state_t state;       /**< Current state */

    /* Configuration */
    jezgro_service_desc_t desc;         /**< Service descriptor */
    bool critical;                      /**< Critical service (must run) */
    bool auto_restart;                  /**< Auto-restart on crash */

    /* Health monitoring */
    uint32_t last_heartbeat_ms;         /**< Last heartbeat time */
    uint8_t missed_heartbeats;          /**< Consecutive missed heartbeats */

    /* Crash tracking */
    uint8_t restart_count;              /**< Total restarts since boot */
    uint8_t consecutive_crashes;        /**< Consecutive crashes */
    jezgro_crash_reason_t last_crash;   /**< Last crash reason */
    uint32_t last_crash_time_ms;        /**< Time of last crash */

    /* Timing */
    uint32_t start_time_ms;             /**< When service started */
    uint32_t total_uptime_ms;           /**< Cumulative uptime */
    uint32_t last_restart_duration_ms;  /**< Last restart time (for metrics) */
} jezgro_service_info_t;

/**
 * @brief Crash callback type
 */
typedef void (*jezgro_crash_callback_t)(uint8_t service_id,
                                         jezgro_crash_reason_t reason);

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize reincarnation server
 *
 * @return EKK_OK on success
 */
ekk_error_t jezgro_rs_init(void);

/**
 * @brief Start reincarnation server
 *
 * Begins health monitoring loop.
 */
void jezgro_rs_start(void);

/**
 * @brief Stop reincarnation server
 */
void jezgro_rs_stop(void);

/* ============================================================================
 * SERVICE MANAGEMENT
 * ============================================================================ */

/**
 * @brief Register a service with the reincarnation server
 *
 * @param desc Service descriptor
 * @param critical Is this a critical service?
 * @param auto_restart Should service auto-restart on crash?
 * @return EKK_OK on success
 */
ekk_error_t jezgro_rs_register_service(const jezgro_service_desc_t* desc,
                                        bool critical,
                                        bool auto_restart);

/**
 * @brief Start a registered service
 *
 * @param service_id Service to start
 * @return EKK_OK on success
 */
ekk_error_t jezgro_rs_start_service(uint8_t service_id);

/**
 * @brief Stop a running service
 *
 * @param service_id Service to stop
 * @param graceful If true, send shutdown message first
 * @return EKK_OK on success
 */
ekk_error_t jezgro_rs_stop_service(uint8_t service_id, bool graceful);

/**
 * @brief Restart a service
 *
 * @param service_id Service to restart
 * @return EKK_OK on success
 */
ekk_error_t jezgro_rs_restart_service(uint8_t service_id);

/**
 * @brief Get service info
 *
 * @param service_id Service to query
 * @param info Output buffer
 * @return EKK_OK on success
 */
ekk_error_t jezgro_rs_get_service_info(uint8_t service_id,
                                        jezgro_service_info_t* info);

/* ============================================================================
 * HEALTH MONITORING
 * ============================================================================ */

/**
 * @brief Tick function - call from main loop or timer
 *
 * Checks service health and triggers restarts as needed.
 *
 * @param now_ms Current time in milliseconds
 */
void jezgro_rs_tick(uint32_t now_ms);

/**
 * @brief Record heartbeat from service
 *
 * Called when service responds to heartbeat ping.
 *
 * @param service_id Service that sent heartbeat
 */
void jezgro_rs_heartbeat(uint8_t service_id);

/**
 * @brief Report service crash
 *
 * Called from MPU fault handler or exception handler.
 *
 * @param service_id Service that crashed
 * @param reason Crash reason
 */
void jezgro_rs_report_crash(uint8_t service_id, jezgro_crash_reason_t reason);

/**
 * @brief Set crash callback
 *
 * Called when any service crashes (before restart).
 *
 * @param callback Callback function
 */
void jezgro_rs_set_crash_callback(jezgro_crash_callback_t callback);

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

/**
 * @brief Reincarnation server statistics
 */
typedef struct {
    uint32_t total_restarts;            /**< Total service restarts */
    uint32_t mpu_faults;                /**< MPU fault count */
    uint32_t heartbeat_timeouts;        /**< Heartbeat timeout count */
    uint32_t avg_restart_time_us;       /**< Average restart time */
    uint32_t max_restart_time_us;       /**< Maximum restart time */
    uint32_t min_restart_time_us;       /**< Minimum restart time */
    uint8_t services_running;           /**< Currently running services */
    uint8_t services_dead;              /**< Permanently dead services */
} jezgro_rs_stats_t;

/**
 * @brief Get RS statistics
 *
 * @param stats Output buffer
 */
void jezgro_rs_get_stats(jezgro_rs_stats_t* stats);

/**
 * @brief Reset RS statistics
 */
void jezgro_rs_reset_stats(void);

/* ============================================================================
 * DEBUG
 * ============================================================================ */

/**
 * @brief Dump RS state to debug output
 */
void jezgro_rs_dump_state(void);

/**
 * @brief Dump service state
 *
 * @param service_id Service to dump
 */
void jezgro_rs_dump_service(uint8_t service_id);

#ifdef __cplusplus
}
#endif

#endif /* JEZGRO_REINCARNATION_H */
