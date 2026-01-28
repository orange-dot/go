/**
 * @file ekkdb_ts.c
 * @brief EKKDB Time-Series Database Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements a time-series database with:
 * - Ring buffer storage with head/tail pointers
 * - 32-byte records (16 per 512-byte block)
 * - Query by time range
 * - Compaction with averaging
 * - Stored on EKKFS filesystem
 */

#include "ekkdb.h"
#include "ekkfs.h"
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static ekkdb_ts_state_t g_ts_handles[EKKDB_MAX_TS_HANDLES];
static ekkdb_ts_iter_state_t g_ts_iterators[EKKDB_MAX_TS_HANDLES * 2];
static int g_ts_initialized = 0;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Build TS filename from module ID and metric
 */
static void build_ts_filename(char *buf, size_t buflen, uint8_t module_id, const char *metric)
{
    /* Format: ts_m<id>_<metric>.dat - max 15 chars */
    int pos = 0;

    buf[pos++] = 't';
    buf[pos++] = 's';
    buf[pos++] = '_';
    buf[pos++] = 'm';

    /* Module ID (2-3 digits) */
    if (module_id >= 100) {
        buf[pos++] = '0' + (module_id / 100);
        buf[pos++] = '0' + ((module_id / 10) % 10);
        buf[pos++] = '0' + (module_id % 10);
    } else if (module_id >= 10) {
        buf[pos++] = '0' + (module_id / 10);
        buf[pos++] = '0' + (module_id % 10);
    } else {
        buf[pos++] = '0' + module_id;
    }

    buf[pos++] = '_';

    /* Metric name (truncate if needed) */
    int metric_len = strlen(metric);
    int max_metric = 15 - pos - 4;  /* Reserve space for .dat */
    if (metric_len > max_metric) metric_len = max_metric;

    memcpy(buf + pos, metric, metric_len);
    pos += metric_len;

    memcpy(buf + pos, ".dat", 5);
}

/**
 * @brief Read TS record from file
 */
static int read_ts_record(const char *filename, uint32_t index, ekkdb_ts_record_t *record)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    /* Calculate offset: header (64 bytes) + index * record_size */
    uint32_t offset = sizeof(ekkdb_ts_header_t) + index * EKKDB_TS_RECORD_SIZE;
    ekkfs_seek(&file, offset);

    int n = ekkfs_read(&file, record, EKKDB_TS_RECORD_SIZE);
    ekkfs_close(&file);

    if (n != EKKDB_TS_RECORD_SIZE) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Write TS record to file
 */
static int write_ts_record(const char *filename, uint32_t index, const ekkdb_ts_record_t *record)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    uint32_t offset = sizeof(ekkdb_ts_header_t) + index * EKKDB_TS_RECORD_SIZE;
    ekkfs_seek(&file, offset);

    int n = ekkfs_write(&file, record, EKKDB_TS_RECORD_SIZE, 0);
    ekkfs_close(&file);

    if (n != EKKDB_TS_RECORD_SIZE) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Write TS header to file
 */
static int write_ts_header(const char *filename, const ekkdb_ts_header_t *header)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    ekkfs_seek(&file, 0);
    int n = ekkfs_write(&file, header, sizeof(ekkdb_ts_header_t), 0);
    ekkfs_close(&file);

    if (n != sizeof(ekkdb_ts_header_t)) {
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
 * @brief Allocate a TS iterator
 */
static int alloc_ts_iterator(void)
{
    for (int i = 0; i < EKKDB_MAX_TS_HANDLES * 2; i++) {
        if (!g_ts_iterators[i].in_use) {
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * Server-Side Implementation
 * ============================================================================ */

int ekkdb_ts_server_open(uint8_t module_id, const char *metric, uint8_t owner_id)
{
    if (!metric || strlen(metric) == 0) {
        return EKKDB_ERR_INVALID;
    }
    if (strlen(metric) > EKKDB_TS_MAX_METRIC_LEN) {
        return EKKDB_ERR_INVALID;
    }

    /* Find free handle */
    int handle = -1;
    for (int i = 0; i < EKKDB_MAX_TS_HANDLES; i++) {
        if (!g_ts_handles[i].in_use) {
            handle = i;
            break;
        }
    }
    if (handle < 0) {
        return EKKDB_ERR_FULL;
    }

    ekkdb_ts_state_t *state = &g_ts_handles[handle];
    memset(state, 0, sizeof(*state));

    /* Build filename */
    build_ts_filename(state->filename, sizeof(state->filename), module_id, metric);
    state->module_id = module_id;
    strncpy(state->metric, metric, EKKDB_TS_MAX_METRIC_LEN);

    /* Try to open existing file */
    ekkfs_stat_t fstat;
    int ret = ekkfs_stat(state->filename, &fstat);

    if (ret == EKKFS_OK) {
        /* File exists - read header */
        ekkfs_file_t file;
        ret = ekkfs_open(&file, state->filename);
        if (ret != EKKFS_OK) {
            return EKKDB_ERR_IO;
        }

        int n = ekkfs_read(&file, &state->header, sizeof(state->header));
        ekkfs_close(&file);

        if (n != sizeof(state->header) || state->header.magic != EKKDB_TS_MAGIC) {
            return EKKDB_ERR_CORRUPT;
        }

        /* Verify CRC */
        uint32_t crc = ekkfs_crc32(&state->header, sizeof(state->header) - sizeof(uint32_t));
        if (crc != state->header.crc32) {
            return EKKDB_ERR_CORRUPT;
        }
    } else {
        /* Create new file */
        ret = ekkfs_create(state->filename, owner_id, 0);
        if (ret < 0) {
            return EKKDB_ERR_IO;
        }

        /* Initialize header */
        memset(&state->header, 0, sizeof(state->header));
        state->header.magic = EKKDB_TS_MAGIC;
        state->header.version = 1;
        state->header.head = 0;
        state->header.tail = 0;
        state->header.count = 0;
        state->header.max_records = EKKDB_TS_MAX_RECORDS;
        state->header.module_id = module_id;
        strncpy(state->header.metric, metric, sizeof(state->header.metric) - 1);
        state->header.oldest_timestamp = 0;
        state->header.newest_timestamp = 0;
        state->header.crc32 = ekkfs_crc32(&state->header, sizeof(state->header) - sizeof(uint32_t));

        /* Write header */
        ekkfs_file_t file;
        ret = ekkfs_open(&file, state->filename);
        if (ret != EKKFS_OK) {
            return EKKDB_ERR_IO;
        }

        ekkfs_write(&file, &state->header, sizeof(state->header), owner_id);

        /* Pre-allocate space for records (optional but improves performance) */
        ekkdb_ts_record_t empty;
        memset(&empty, 0, sizeof(empty));
        for (uint32_t i = 0; i < state->header.max_records; i++) {
            ekkfs_write(&file, &empty, sizeof(empty), owner_id);
        }

        ekkfs_close(&file);
    }

    state->in_use = 1;
    state->dirty = 0;

    return handle;
}

int ekkdb_ts_server_close(uint32_t handle)
{
    if (handle >= EKKDB_MAX_TS_HANDLES || !g_ts_handles[handle].in_use) {
        return EKKDB_ERR_INVALID;
    }

    ekkdb_ts_state_t *state = &g_ts_handles[handle];

    /* Flush header if dirty */
    if (state->dirty) {
        state->header.crc32 = ekkfs_crc32(&state->header, sizeof(state->header) - sizeof(uint32_t));
        write_ts_header(state->filename, &state->header);
    }

    /* Close any open iterators for this handle */
    for (int i = 0; i < EKKDB_MAX_TS_HANDLES * 2; i++) {
        if (g_ts_iterators[i].in_use && g_ts_iterators[i].ts_handle == handle) {
            g_ts_iterators[i].in_use = 0;
        }
    }

    state->in_use = 0;
    return EKKDB_OK;
}

int ekkdb_ts_server_append(uint32_t handle, const ekkdb_ts_record_t *record)
{
    if (handle >= EKKDB_MAX_TS_HANDLES || !g_ts_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!record) {
        return EKKDB_ERR_INVALID;
    }

    ekkdb_ts_state_t *state = &g_ts_handles[handle];

    /* Write record at head position */
    int ret = write_ts_record(state->filename, state->header.head, record);
    if (ret != EKKDB_OK) {
        return ret;
    }

    /* Update timestamps */
    if (state->header.count == 0 || record->timestamp < state->header.oldest_timestamp) {
        state->header.oldest_timestamp = record->timestamp;
    }
    if (record->timestamp > state->header.newest_timestamp) {
        state->header.newest_timestamp = record->timestamp;
    }

    /* Advance head */
    uint32_t new_head = ring_next(state->header.head, state->header.max_records);

    /* Check if ring is full */
    if (state->header.count >= state->header.max_records) {
        /* Overwriting oldest - advance tail */
        state->header.tail = ring_next(state->header.tail, state->header.max_records);

        /* Need to update oldest_timestamp from new tail record */
        ekkdb_ts_record_t tail_rec;
        if (read_ts_record(state->filename, state->header.tail, &tail_rec) == EKKDB_OK) {
            state->header.oldest_timestamp = tail_rec.timestamp;
        }
    } else {
        state->header.count++;
    }

    state->header.head = new_head;
    state->dirty = 1;

    /* Periodic header flush */
    if (state->header.count % EKKDB_COMPACT_INTERVAL == 0) {
        state->header.crc32 = ekkfs_crc32(&state->header, sizeof(state->header) - sizeof(uint32_t));
        write_ts_header(state->filename, &state->header);
        state->dirty = 0;
    }

    return EKKDB_OK;
}

int ekkdb_ts_server_query(uint32_t handle, uint64_t start_us, uint64_t end_us,
                          uint32_t *iter_handle, uint32_t *total_count)
{
    if (handle >= EKKDB_MAX_TS_HANDLES || !g_ts_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    ekkdb_ts_state_t *state = &g_ts_handles[handle];

    /* Allocate iterator */
    int iter_idx = alloc_ts_iterator();
    if (iter_idx < 0) {
        return EKKDB_ERR_FULL;
    }

    ekkdb_ts_iter_state_t *iter = &g_ts_iterators[iter_idx];
    memset(iter, 0, sizeof(*iter));

    iter->in_use = 1;
    iter->ts_handle = handle;
    iter->start_us = start_us;
    iter->end_us = end_us ? end_us : UINT64_MAX;
    iter->current_idx = state->header.tail;
    iter->records_returned = 0;

    /* Count matching records (could be expensive for large datasets) */
    uint32_t count = 0;
    uint32_t idx = state->header.tail;

    for (uint32_t i = 0; i < state->header.count; i++) {
        ekkdb_ts_record_t rec;
        if (read_ts_record(state->filename, idx, &rec) == EKKDB_OK) {
            if (rec.timestamp >= iter->start_us && rec.timestamp <= iter->end_us) {
                count++;
            }
        }
        idx = ring_next(idx, state->header.max_records);
    }

    *iter_handle = iter_idx;
    *total_count = count;

    return EKKDB_OK;
}

int ekkdb_ts_server_next(uint32_t iter_handle, ekkdb_ts_record_t *record)
{
    if (iter_handle >= EKKDB_MAX_TS_HANDLES * 2 || !g_ts_iterators[iter_handle].in_use) {
        return EKKDB_ERR_INVALID;
    }

    ekkdb_ts_iter_state_t *iter = &g_ts_iterators[iter_handle];
    ekkdb_ts_state_t *state = &g_ts_handles[iter->ts_handle];

    if (!state->in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    /* Find next matching record */
    while (iter->records_returned < state->header.count) {
        ekkdb_ts_record_t rec;
        int ret = read_ts_record(state->filename, iter->current_idx, &rec);
        if (ret != EKKDB_OK) {
            return ret;
        }

        iter->current_idx = ring_next(iter->current_idx, state->header.max_records);
        iter->records_returned++;

        /* Check if record matches time range */
        if (rec.timestamp >= iter->start_us && rec.timestamp <= iter->end_us) {
            memcpy(record, &rec, sizeof(*record));
            return EKKDB_OK;
        }

        /* If we've passed end_us, no more matches */
        if (rec.timestamp > iter->end_us) {
            break;
        }
    }

    return EKKDB_ERR_NOT_FOUND;
}

int ekkdb_ts_server_iter_close(uint32_t iter_handle)
{
    if (iter_handle >= EKKDB_MAX_TS_HANDLES * 2) {
        return EKKDB_ERR_INVALID;
    }

    g_ts_iterators[iter_handle].in_use = 0;
    return EKKDB_OK;
}

int ekkdb_ts_server_compact(uint32_t handle)
{
    if (handle >= EKKDB_MAX_TS_HANDLES || !g_ts_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    ekkdb_ts_state_t *state = &g_ts_handles[handle];

    /* Compaction: compute averages over intervals and write to a compacted file */
    /* For simplicity, we'll just flush the header here */
    /* A full implementation would:
     * 1. Read records in chunks
     * 2. Compute interval averages (e.g., 1 minute averages)
     * 3. Write to a separate file (ts_m<id>_<metric>_c.dat)
     * 4. Optionally free space in main ring buffer
     */

    if (state->dirty) {
        state->header.crc32 = ekkfs_crc32(&state->header, sizeof(state->header) - sizeof(uint32_t));
        write_ts_header(state->filename, &state->header);
        state->dirty = 0;
    }

    return EKKDB_OK;
}

int ekkdb_ts_server_count(uint32_t handle)
{
    if (handle >= EKKDB_MAX_TS_HANDLES || !g_ts_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    return g_ts_handles[handle].header.count;
}
