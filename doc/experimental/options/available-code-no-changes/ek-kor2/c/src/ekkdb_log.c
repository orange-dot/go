/**
 * @file ekkdb_log.c
 * @brief EKKDB Event Log Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements an event log database with:
 * - Append-only ring buffer
 * - 64-byte event records (8 per 512-byte block)
 * - Query by time range, severity, source
 * - CRC32 integrity checking
 * - Stored on EKKFS filesystem
 */

#include "ekkdb.h"
#include "ekkfs.h"
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static ekkdb_log_state_t g_log_handle;
static ekkdb_log_iter_state_t g_log_iterators[4];  /* Max 4 concurrent queries */
static int g_log_initialized = 0;

#define LOG_FILENAME    "log_event.dat"

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Read event from file
 */
static int read_log_event(const char *filename, uint32_t index, ekkdb_event_t *event)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    /* Calculate offset: header (64 bytes) + index * event_size */
    uint32_t offset = sizeof(ekkdb_log_header_t) + index * EKKDB_EVENT_SIZE;
    ekkfs_seek(&file, offset);

    int n = ekkfs_read(&file, event, EKKDB_EVENT_SIZE);
    ekkfs_close(&file);

    if (n != EKKDB_EVENT_SIZE) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Write event to file
 */
static int write_log_event(const char *filename, uint32_t index, const ekkdb_event_t *event)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    uint32_t offset = sizeof(ekkdb_log_header_t) + index * EKKDB_EVENT_SIZE;
    ekkfs_seek(&file, offset);

    int n = ekkfs_write(&file, event, EKKDB_EVENT_SIZE, 0);
    ekkfs_close(&file);

    if (n != EKKDB_EVENT_SIZE) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Write log header to file
 */
static int write_log_header(const char *filename, const ekkdb_log_header_t *header)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    ekkfs_seek(&file, 0);
    int n = ekkfs_write(&file, header, sizeof(ekkdb_log_header_t), 0);
    ekkfs_close(&file);

    if (n != sizeof(ekkdb_log_header_t)) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Get the next index in ring buffer
 */
static uint32_t ring_next(uint32_t idx, uint32_t max)
{
    return (idx + 1) % max;
}

/**
 * @brief Check if event matches filter
 */
static int event_matches_filter(const ekkdb_event_t *event, const ekkdb_log_filter_t *filter)
{
    if (!filter) return 1;  /* No filter = match all */

    /* Time range */
    if (filter->start_us > 0 && event->timestamp < filter->start_us) {
        return 0;
    }
    if (filter->end_us > 0 && event->timestamp > filter->end_us) {
        return 0;
    }

    /* Severity */
    if (event->severity < filter->min_severity) {
        return 0;
    }

    /* Source type */
    if (filter->source_type != 0xFF && event->source_type != filter->source_type) {
        return 0;
    }

    /* Source ID */
    if (filter->source_id != 0xFF && event->source_id != filter->source_id) {
        return 0;
    }

    return 1;
}

/**
 * @brief Allocate a log iterator
 */
static int alloc_log_iterator(void)
{
    for (int i = 0; i < 4; i++) {
        if (!g_log_iterators[i].in_use) {
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * Server-Side Implementation
 * ============================================================================ */

int ekkdb_log_server_open(uint8_t owner_id)
{
    if (g_log_handle.in_use) {
        /* Already open - return existing handle */
        return 0;
    }

    memset(&g_log_handle, 0, sizeof(g_log_handle));
    strncpy(g_log_handle.filename, LOG_FILENAME, sizeof(g_log_handle.filename) - 1);

    /* Try to open existing file */
    ekkfs_stat_t fstat;
    int ret = ekkfs_stat(g_log_handle.filename, &fstat);

    if (ret == EKKFS_OK) {
        /* File exists - read header */
        ekkfs_file_t file;
        ret = ekkfs_open(&file, g_log_handle.filename);
        if (ret != EKKFS_OK) {
            return EKKDB_ERR_IO;
        }

        int n = ekkfs_read(&file, &g_log_handle.header, sizeof(g_log_handle.header));
        ekkfs_close(&file);

        if (n != sizeof(g_log_handle.header) || g_log_handle.header.magic != EKKDB_LOG_MAGIC) {
            return EKKDB_ERR_CORRUPT;
        }

        /* Verify CRC */
        uint32_t crc = ekkfs_crc32(&g_log_handle.header,
                                   sizeof(g_log_handle.header) - sizeof(g_log_handle.header.padding) - sizeof(uint32_t));
        if (crc != g_log_handle.header.crc32) {
            return EKKDB_ERR_CORRUPT;
        }
    } else {
        /* Create new file */
        ret = ekkfs_create(g_log_handle.filename, owner_id, EKKFS_FLAG_LOG);
        if (ret < 0) {
            return EKKDB_ERR_IO;
        }

        /* Initialize header */
        memset(&g_log_handle.header, 0, sizeof(g_log_handle.header));
        g_log_handle.header.magic = EKKDB_LOG_MAGIC;
        g_log_handle.header.version = 1;
        g_log_handle.header.head = 0;
        g_log_handle.header.tail = 0;
        g_log_handle.header.count = 0;
        g_log_handle.header.max_events = EKKDB_LOG_MAX_EVENTS;
        g_log_handle.header.next_sequence = 1;
        g_log_handle.header.oldest_timestamp = 0;
        g_log_handle.header.newest_timestamp = 0;
        g_log_handle.header.crc32 = ekkfs_crc32(&g_log_handle.header,
                                                sizeof(g_log_handle.header) - sizeof(g_log_handle.header.padding) - sizeof(uint32_t));

        /* Write header */
        ekkfs_file_t file;
        ret = ekkfs_open(&file, g_log_handle.filename);
        if (ret != EKKFS_OK) {
            return EKKDB_ERR_IO;
        }

        ekkfs_write(&file, &g_log_handle.header, sizeof(g_log_handle.header), owner_id);

        /* Pre-allocate space for events */
        ekkdb_event_t empty;
        memset(&empty, 0, sizeof(empty));
        for (uint32_t i = 0; i < g_log_handle.header.max_events; i++) {
            ekkfs_write(&file, &empty, sizeof(empty), owner_id);
        }

        ekkfs_close(&file);
    }

    g_log_handle.in_use = 1;
    g_log_handle.dirty = 0;

    return 0;  /* Log always uses handle 0 */
}

int ekkdb_log_server_close(uint32_t handle)
{
    if (handle != 0 || !g_log_handle.in_use) {
        return EKKDB_ERR_INVALID;
    }

    /* Flush header if dirty */
    if (g_log_handle.dirty) {
        g_log_handle.header.crc32 = ekkfs_crc32(&g_log_handle.header,
                                                sizeof(g_log_handle.header) - sizeof(g_log_handle.header.padding) - sizeof(uint32_t));
        write_log_header(g_log_handle.filename, &g_log_handle.header);
    }

    /* Close any open iterators */
    for (int i = 0; i < 4; i++) {
        g_log_iterators[i].in_use = 0;
    }

    g_log_handle.in_use = 0;
    return EKKDB_OK;
}

int ekkdb_log_server_write(uint32_t handle, const ekkdb_event_t *event)
{
    if (handle != 0 || !g_log_handle.in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!event) {
        return EKKDB_ERR_INVALID;
    }

    /* Copy event and set/update fields */
    ekkdb_event_t evt;
    memcpy(&evt, event, sizeof(evt));

    /* Set timestamp if not provided */
    if (evt.timestamp == 0) {
        evt.timestamp = ekkfs_get_time_us();
    }

    /* Set sequence number */
    evt.sequence = g_log_handle.header.next_sequence++;

    /* Calculate CRC (exclude the CRC field itself) */
    evt.crc32 = ekkfs_crc32(&evt, sizeof(evt) - sizeof(evt.crc32));

    /* Write event at head position */
    int ret = write_log_event(g_log_handle.filename, g_log_handle.header.head, &evt);
    if (ret != EKKDB_OK) {
        return ret;
    }

    /* Update timestamps */
    if (g_log_handle.header.count == 0 || evt.timestamp < g_log_handle.header.oldest_timestamp) {
        g_log_handle.header.oldest_timestamp = evt.timestamp;
    }
    if (evt.timestamp > g_log_handle.header.newest_timestamp) {
        g_log_handle.header.newest_timestamp = evt.timestamp;
    }

    /* Advance head */
    uint32_t new_head = ring_next(g_log_handle.header.head, g_log_handle.header.max_events);

    /* Check if ring is full */
    if (g_log_handle.header.count >= g_log_handle.header.max_events) {
        /* Overwriting oldest - advance tail */
        g_log_handle.header.tail = ring_next(g_log_handle.header.tail, g_log_handle.header.max_events);

        /* Update oldest_timestamp from new tail event */
        ekkdb_event_t tail_evt;
        if (read_log_event(g_log_handle.filename, g_log_handle.header.tail, &tail_evt) == EKKDB_OK) {
            g_log_handle.header.oldest_timestamp = tail_evt.timestamp;
        }
    } else {
        g_log_handle.header.count++;
    }

    g_log_handle.header.head = new_head;
    g_log_handle.dirty = 1;

    /* Periodic header flush (every 8 events) */
    if (g_log_handle.header.count % 8 == 0) {
        g_log_handle.header.crc32 = ekkfs_crc32(&g_log_handle.header,
                                                sizeof(g_log_handle.header) - sizeof(g_log_handle.header.padding) - sizeof(uint32_t));
        write_log_header(g_log_handle.filename, &g_log_handle.header);
        g_log_handle.dirty = 0;
    }

    return EKKDB_OK;
}

int ekkdb_log_server_query(uint32_t handle, const ekkdb_log_filter_t *filter,
                           uint32_t *iter_handle, uint32_t *total_count)
{
    if (handle != 0 || !g_log_handle.in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    /* Allocate iterator */
    int iter_idx = alloc_log_iterator();
    if (iter_idx < 0) {
        return EKKDB_ERR_FULL;
    }

    ekkdb_log_iter_state_t *iter = &g_log_iterators[iter_idx];
    memset(iter, 0, sizeof(*iter));

    iter->in_use = 1;
    iter->current_idx = g_log_handle.header.tail;
    iter->events_returned = 0;

    if (filter) {
        memcpy(&iter->filter, filter, sizeof(*filter));
    } else {
        /* Default filter: match all */
        iter->filter.start_us = 0;
        iter->filter.end_us = 0;
        iter->filter.min_severity = EKKDB_SEV_DEBUG;
        iter->filter.source_type = 0xFF;
        iter->filter.source_id = 0xFF;
    }

    /* Count matching events */
    uint32_t count = 0;
    uint32_t idx = g_log_handle.header.tail;

    for (uint32_t i = 0; i < g_log_handle.header.count; i++) {
        ekkdb_event_t evt;
        if (read_log_event(g_log_handle.filename, idx, &evt) == EKKDB_OK) {
            if (event_matches_filter(&evt, &iter->filter)) {
                count++;
            }
        }
        idx = ring_next(idx, g_log_handle.header.max_events);
    }

    *iter_handle = iter_idx;
    *total_count = count;

    return EKKDB_OK;
}

int ekkdb_log_server_next(uint32_t iter_handle, ekkdb_event_t *event)
{
    if (iter_handle >= 4 || !g_log_iterators[iter_handle].in_use) {
        return EKKDB_ERR_INVALID;
    }

    if (!g_log_handle.in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    ekkdb_log_iter_state_t *iter = &g_log_iterators[iter_handle];

    /* Find next matching event */
    while (iter->events_returned < g_log_handle.header.count) {
        ekkdb_event_t evt;
        int ret = read_log_event(g_log_handle.filename, iter->current_idx, &evt);
        if (ret != EKKDB_OK) {
            return ret;
        }

        iter->current_idx = ring_next(iter->current_idx, g_log_handle.header.max_events);
        iter->events_returned++;

        /* Check if event matches filter */
        if (event_matches_filter(&evt, &iter->filter)) {
            /* Verify CRC */
            uint32_t crc = ekkfs_crc32(&evt, sizeof(evt) - sizeof(evt.crc32));
            if (crc != evt.crc32) {
                /* CRC mismatch - event may be corrupted, skip it */
                continue;
            }

            memcpy(event, &evt, sizeof(*event));
            return EKKDB_OK;
        }
    }

    return EKKDB_ERR_NOT_FOUND;
}

int ekkdb_log_server_iter_close(uint32_t iter_handle)
{
    if (iter_handle >= 4) {
        return EKKDB_ERR_INVALID;
    }

    g_log_iterators[iter_handle].in_use = 0;
    return EKKDB_OK;
}

int ekkdb_log_server_count(uint32_t handle)
{
    if (handle != 0 || !g_log_handle.in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    return g_log_handle.header.count;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

void ekkdb_event_init(ekkdb_event_t *event, uint8_t severity, uint8_t source_type,
                      uint8_t source_id, uint32_t event_code, const char *message)
{
    if (!event) return;

    memset(event, 0, sizeof(*event));
    event->timestamp = ekkfs_get_time_us();
    event->severity = severity;
    event->source_type = source_type;
    event->source_id = source_id;
    event->event_code = event_code;

    if (message) {
        strncpy(event->message, message, EKKDB_EVENT_MSG_LEN - 1);
        event->message[EKKDB_EVENT_MSG_LEN - 1] = '\0';
    }
}

void ekkdb_ts_record_init(ekkdb_ts_record_t *record, uint16_t module_id,
                          int32_t voltage_mv, int32_t current_ma,
                          int32_t temp_mc, int32_t power_mw)
{
    if (!record) return;

    memset(record, 0, sizeof(*record));
    record->timestamp = ekkfs_get_time_us();
    record->module_id = module_id;
    record->voltage_mv = voltage_mv;
    record->current_ma = current_ma;
    record->temp_mc = temp_mc;
    record->power_mw = power_mw;
}
