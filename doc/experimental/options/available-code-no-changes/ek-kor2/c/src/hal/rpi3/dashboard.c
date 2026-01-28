/**
 * @file dashboard.c
 * @brief Top-Like Dashboard Implementation for EK-KOR v2
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#include "dashboard.h"
#include "framebuffer.h"
#include "timer.h"
#include "smp.h"
#include <string.h>

/* ============================================================================
 * Layout Constants
 * ============================================================================ */

/* Screen layout (character positions for 2x scale font) */
#define HEADER_ROW          0
#define CORE_HEADER_ROW     2
#define CORE_START_ROW      3
#define STATS_ROW           8
#define LOG_HEADER_ROW      10
#define LOG_START_ROW       11

/* Column positions */
#define COL_CORE            2
#define COL_STATE           8
#define COL_LOAD            18
#define COL_MSGS            26
#define COL_HEALTH          34

/* ============================================================================
 * State Variables
 * ============================================================================ */

/* Core information */
typedef struct {
    core_state_t state;
    uint32_t load_pct;
    volatile uint32_t msg_count;
} core_info_t;

static core_info_t g_cores[DASHBOARD_NUM_CORES];

/* System statistics */
static volatile uint64_t g_total_ticks = 0;
static volatile uint64_t g_total_msgs = 0;
static volatile uint64_t g_total_heartbeats = 0;

/* Log ring buffer */
static char g_log_buffer[DASHBOARD_LOG_LINES][DASHBOARD_LOG_LINE_LEN];
static uint32_t g_log_head = 0;
static uint32_t g_log_count = 0;

/* Spinlock for log access */
static smp_spinlock_t g_log_lock = SMP_SPINLOCK_INIT;

/* Dashboard initialized flag */
static int g_dashboard_ready = 0;

/* Start time for uptime calculation */
static uint64_t g_start_time_us = 0;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Format uptime as HH:MM:SS
 */
static void format_uptime(uint64_t uptime_us, char *buf, uint32_t buflen)
{
    uint32_t secs = (uint32_t)(uptime_us / 1000000);
    uint32_t hours = secs / 3600;
    uint32_t mins = (secs % 3600) / 60;
    uint32_t sec = secs % 60;

    /* Manual formatting to avoid large buffers */
    if (buflen < 9) return;

    buf[0] = '0' + (hours / 10) % 10;
    buf[1] = '0' + hours % 10;
    buf[2] = ':';
    buf[3] = '0' + mins / 10;
    buf[4] = '0' + mins % 10;
    buf[5] = ':';
    buf[6] = '0' + sec / 10;
    buf[7] = '0' + sec % 10;
    buf[8] = '\0';
}

/**
 * @brief Format timestamp for log [MM:SS]
 */
static void format_timestamp(uint64_t time_us, char *buf, uint32_t buflen)
{
    uint32_t secs = (uint32_t)(time_us / 1000000);
    uint32_t mins = (secs / 60) % 60;
    uint32_t sec = secs % 60;

    if (buflen < 8) return;

    buf[0] = '[';
    buf[1] = '0' + mins / 10;
    buf[2] = '0' + mins % 10;
    buf[3] = ':';
    buf[4] = '0' + sec / 10;
    buf[5] = '0' + sec % 10;
    buf[6] = ']';
    buf[7] = '\0';
}

/**
 * @brief Get state string
 */
static const char* state_to_str(core_state_t state)
{
    switch (state) {
        case CORE_STATE_OFFLINE: return "OFFLINE";
        case CORE_STATE_BOOTING: return "BOOTING";
        case CORE_STATE_ACTIVE:  return "ACTIVE ";
        case CORE_STATE_IDLE:    return "IDLE   ";
        default:                 return "UNKNOWN";
    }
}

/**
 * @brief Draw load bar (3 characters)
 */
static void draw_load_bar(uint32_t col, uint32_t row, uint32_t pct)
{
    fb_set_cursor(col, row);

    /* Simple 3-char bar: each char = 33% */
    uint32_t bars = (pct + 16) / 33;
    if (bars > 3) bars = 3;

    for (uint32_t i = 0; i < 3; i++) {
        fb_putchar(i < bars ? '#' : '-');
    }
}

/**
 * @brief Clear a line (fill with spaces)
 */
static void clear_line(uint32_t row, uint32_t start_col, uint32_t len)
{
    fb_set_cursor(start_col, row);
    for (uint32_t i = 0; i < len; i++) {
        fb_putchar(' ');
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void dashboard_init(void)
{
    if (!framebuffer_is_ready()) {
        return;
    }

    /* Store start time */
    g_start_time_us = timer_get_us();

    /* Initialize core states */
    for (int i = 0; i < DASHBOARD_NUM_CORES; i++) {
        g_cores[i].state = CORE_STATE_OFFLINE;
        g_cores[i].load_pct = 0;
        g_cores[i].msg_count = 0;
    }

    /* Clear log buffer */
    memset(g_log_buffer, 0, sizeof(g_log_buffer));
    g_log_head = 0;
    g_log_count = 0;

    /* Clear screen */
    fb_clear(FB_COLOR_BLACK);

    /* Draw static header */
    fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
    fb_set_cursor(0, HEADER_ROW);
    fb_puts("         EK-KOR v2 Dashboard           Uptime: 00:00:00");

    /* Draw separator */
    fb_set_colors(FB_COLOR_GRAY, FB_COLOR_BLACK);
    fb_set_cursor(0, HEADER_ROW + 1);
    for (int i = 0; i < 58; i++) fb_putchar('-');

    /* Draw core table header */
    fb_set_colors(FB_COLOR_YELLOW, FB_COLOR_BLACK);
    fb_set_cursor(0, CORE_HEADER_ROW);
    fb_puts(" CORE  STATE     LOAD   MSGS   HEALTH");

    /* Draw stats separator */
    fb_set_colors(FB_COLOR_GRAY, FB_COLOR_BLACK);
    fb_set_cursor(0, STATS_ROW - 1);
    for (int i = 0; i < 58; i++) fb_putchar('-');

    /* Stats label */
    fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    fb_set_cursor(0, STATS_ROW);
    fb_puts(" SYSTEM: Ticks=0       Messages=0       Heartbeats=0");

    /* Log header */
    fb_set_colors(FB_COLOR_GRAY, FB_COLOR_BLACK);
    fb_set_cursor(0, LOG_HEADER_ROW - 1);
    for (int i = 0; i < 58; i++) fb_putchar('-');
    fb_set_cursor(22, LOG_HEADER_ROW - 1);
    fb_puts(" Log ");

    g_dashboard_ready = 1;

    dashboard_log("Dashboard initialized");
}

void dashboard_update(void)
{
    if (!g_dashboard_ready) {
        return;
    }

    /* Update uptime */
    uint64_t now = timer_get_us();
    uint64_t uptime = now - g_start_time_us;
    char uptime_str[12];
    format_uptime(uptime, uptime_str, sizeof(uptime_str));

    fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
    fb_set_cursor(48, HEADER_ROW);
    fb_puts(uptime_str);

    /* Update core rows */
    fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    for (int i = 0; i < DASHBOARD_NUM_CORES; i++) {
        uint32_t row = CORE_START_ROW + i;

        /* Core ID */
        fb_set_cursor(COL_CORE, row);
        fb_putchar('0' + i);

        /* State */
        fb_set_cursor(COL_STATE, row);
        fb_puts(state_to_str(g_cores[i].state));

        /* Load percentage */
        fb_set_cursor(COL_LOAD, row);
        uint32_t load = g_cores[i].load_pct;
        fb_putchar('0' + (load / 100) % 10);
        fb_putchar('0' + (load / 10) % 10);
        fb_putchar('0' + load % 10);
        fb_putchar('%');

        /* Message count */
        fb_set_cursor(COL_MSGS, row);
        uint32_t msgs = g_cores[i].msg_count;
        /* Simple 5-digit display */
        fb_putchar('0' + (msgs / 10000) % 10);
        fb_putchar('0' + (msgs / 1000) % 10);
        fb_putchar('0' + (msgs / 100) % 10);
        fb_putchar('0' + (msgs / 10) % 10);
        fb_putchar('0' + msgs % 10);

        /* Health bar */
        draw_load_bar(COL_HEALTH, row, g_cores[i].load_pct);
    }

    /* Update system stats - use simple number printing */
    fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    fb_set_cursor(16, STATS_ROW);
    fb_printf("%llu        ", g_total_ticks);
    fb_set_cursor(35, STATS_ROW);
    fb_printf("%llu        ", g_total_msgs);
    fb_set_cursor(55, STATS_ROW);
    fb_printf("%llu      ", g_total_heartbeats);

    /* Draw log lines */
    fb_set_colors(FB_COLOR_CYAN, FB_COLOR_BLACK);
    uint32_t display_count = g_log_count < DASHBOARD_LOG_LINES ?
                             g_log_count : DASHBOARD_LOG_LINES;

    for (uint32_t i = 0; i < DASHBOARD_LOG_LINES; i++) {
        uint32_t row = LOG_START_ROW + i;
        clear_line(row, 1, DASHBOARD_LOG_LINE_LEN + 2);

        if (i < display_count) {
            /* Calculate index in ring buffer */
            uint32_t idx;
            if (g_log_count <= DASHBOARD_LOG_LINES) {
                idx = i;
            } else {
                idx = (g_log_head + DASHBOARD_LOG_LINES - display_count + i)
                      % DASHBOARD_LOG_LINES;
            }

            fb_set_cursor(1, row);
            fb_puts(g_log_buffer[idx]);
        }
    }
}

void dashboard_log(const char *msg)
{
    if (!g_dashboard_ready || msg == NULL) {
        return;
    }

    smp_spinlock_lock(&g_log_lock);

    /* Format: [MM:SS] message */
    uint64_t now = timer_get_us() - g_start_time_us;
    char timestamp[8];
    format_timestamp(now, timestamp, sizeof(timestamp));

    /* Build log line */
    char *dest = g_log_buffer[g_log_head];
    uint32_t pos = 0;

    /* Copy timestamp */
    for (uint32_t i = 0; timestamp[i] && pos < DASHBOARD_LOG_LINE_LEN - 2; i++) {
        dest[pos++] = timestamp[i];
    }
    dest[pos++] = ' ';

    /* Copy message */
    while (*msg && pos < DASHBOARD_LOG_LINE_LEN - 1) {
        dest[pos++] = *msg++;
    }
    dest[pos] = '\0';

    /* Advance ring buffer */
    g_log_head = (g_log_head + 1) % DASHBOARD_LOG_LINES;
    if (g_log_count < DASHBOARD_LOG_LINES) {
        g_log_count++;
    }

    smp_spinlock_unlock(&g_log_lock);
}

void dashboard_update_core(uint32_t core_id, core_state_t state,
                           uint32_t load_pct, uint32_t msg_count)
{
    if (core_id >= DASHBOARD_NUM_CORES) {
        return;
    }

    g_cores[core_id].state = state;
    g_cores[core_id].load_pct = load_pct > 100 ? 100 : load_pct;
    g_cores[core_id].msg_count = msg_count;
}

void dashboard_update_stats(uint64_t ticks, uint64_t msgs, uint64_t heartbeats)
{
    g_total_ticks = ticks;
    g_total_msgs = msgs;
    g_total_heartbeats = heartbeats;
}

void dashboard_core_active(uint32_t core_id)
{
    if (core_id >= DASHBOARD_NUM_CORES) {
        return;
    }
    g_cores[core_id].state = CORE_STATE_ACTIVE;
}

void dashboard_core_msg_inc(uint32_t core_id)
{
    if (core_id >= DASHBOARD_NUM_CORES) {
        return;
    }
    /* Atomic increment would be better, but simple increment is OK for demo */
    g_cores[core_id].msg_count++;
}
