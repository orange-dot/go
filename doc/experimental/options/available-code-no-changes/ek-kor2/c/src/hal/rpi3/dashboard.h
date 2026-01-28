/**
 * @file dashboard.h
 * @brief Top-Like Dashboard for EK-KOR v2 on Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Provides a real-time "top-like" display showing CPU core status,
 * load, message counts, and system statistics on HDMI output.
 */

#ifndef RPI3_DASHBOARD_H
#define RPI3_DASHBOARD_H

#include <stdint.h>

/* Number of cores to display */
#define DASHBOARD_NUM_CORES     4

/* Log buffer settings */
#define DASHBOARD_LOG_LINES     10
#define DASHBOARD_LOG_LINE_LEN  60

/* Core states */
typedef enum {
    CORE_STATE_OFFLINE = 0,
    CORE_STATE_BOOTING,
    CORE_STATE_ACTIVE,
    CORE_STATE_IDLE
} core_state_t;

/**
 * @brief Initialize the dashboard
 *
 * Must be called after framebuffer_init().
 * Clears the screen and draws initial layout.
 */
void dashboard_init(void);

/**
 * @brief Update the dashboard display
 *
 * Redraws all dynamic elements. Call periodically (e.g., every 100ms).
 */
void dashboard_update(void);

/**
 * @brief Add a log message to the dashboard
 *
 * Adds a timestamped message to the scrolling log area.
 *
 * @param msg Message string (will be truncated if too long)
 */
void dashboard_log(const char *msg);

/**
 * @brief Update core status information
 *
 * @param core_id Core ID (0-3)
 * @param state Core state (CORE_STATE_*)
 * @param load_pct CPU load percentage (0-100)
 * @param msg_count Number of messages processed
 */
void dashboard_update_core(uint32_t core_id, core_state_t state,
                           uint32_t load_pct, uint32_t msg_count);

/**
 * @brief Update system statistics
 *
 * @param ticks Total system ticks
 * @param msgs Total messages processed
 * @param heartbeats Total heartbeats sent
 */
void dashboard_update_stats(uint64_t ticks, uint64_t msgs, uint64_t heartbeats);

/**
 * @brief Mark a core as active
 *
 * Convenience function to set core to ACTIVE state.
 *
 * @param core_id Core ID (0-3)
 */
void dashboard_core_active(uint32_t core_id);

/**
 * @brief Increment message count for a core
 *
 * @param core_id Core ID (0-3)
 */
void dashboard_core_msg_inc(uint32_t core_id);

#endif /* RPI3_DASHBOARD_H */
