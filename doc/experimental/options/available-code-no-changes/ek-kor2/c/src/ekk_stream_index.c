/**
 * @file ekk_stream_index.c
 * @brief EK-KOR v2 - In-Memory Stream Index
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Fast in-memory index for event stream queries. Stores compact index
 * entries in SRAM for O(log n) lookups, with full event data in Flash.
 *
 * QUERY SUPPORT:
 * - By global sequence: O(1) direct lookup
 * - By time range: O(log n) binary search + scan
 * - By stream (module): O(log n) via per-stream tracking
 * - By event type: O(n) scan (type stored in index)
 *
 * MEMORY USAGE (TC397):
 * - 2.9 MB SRAM available
 * - 16 bytes per index entry
 * - ~180K entries = 2.88 MB
 */

#include "ekk/ekk_gateway.h"
#include <string.h>

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/**
 * @brief Binary search for timestamp
 *
 * Finds first entry with timestamp >= target.
 */
static uint32_t binary_search_time(ekk_stream_index_t *index,
                                    uint32_t target_time_us) {
    if (index->count == 0) {
        return 0;
    }

    uint32_t lo = 0;
    uint32_t hi = index->count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (index->entries[mid].timestamp_us < target_time_us) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

/**
 * @brief Binary search for global sequence
 *
 * Entries are appended in sequence order, so we can binary search.
 */
static uint32_t binary_search_seq(ekk_stream_index_t *index,
                                   uint32_t target_seq) {
    if (index->count == 0) {
        return 0;
    }

    uint32_t lo = 0;
    uint32_t hi = index->count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (index->entries[mid].global_seq < target_seq) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

/**
 * @brief Find stream state by module ID
 */
static ekk_stream_state_t *find_stream(ekk_stream_index_t *index,
                                        ekk_module_id_t stream_id) {
    for (uint8_t i = 0; i < index->stream_count; i++) {
        if (index->streams[i].module_id == stream_id) {
            return &index->streams[i];
        }
    }
    return NULL;
}

/**
 * @brief Create new stream entry
 */
static ekk_stream_state_t *create_stream(ekk_stream_index_t *index,
                                          ekk_module_id_t stream_id) {
    if (index->stream_count >= EKK_GATEWAY_MAX_MODULES) {
        return NULL;
    }

    ekk_stream_state_t *stream = &index->streams[index->stream_count];
    memset(stream, 0, sizeof(*stream));
    stream->module_id = stream_id;
    stream->first_index = 0xFFFFFFFF;
    stream->last_index = 0xFFFFFFFF;
    index->stream_count++;

    return stream;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

ekk_error_t ekk_stream_index_init(ekk_stream_index_t *index,
                                   ekk_index_entry_t *buffer,
                                   uint32_t capacity) {
    if (!index || !buffer || capacity == 0) {
        return EKK_ERR_INVALID_ARG;
    }

    memset(index, 0, sizeof(*index));
    index->entries = buffer;
    index->capacity = capacity;
    index->count = 0;
    index->stream_count = 0;
    index->oldest_seq = 0;
    index->newest_seq = 0;

    return EKK_OK;
}

ekk_error_t ekk_stream_index_add(ekk_stream_index_t *index,
                                  const ekk_index_entry_t *entry) {
    if (!index || !entry) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Check capacity */
    if (index->count >= index->capacity) {
        /* Index full - could implement circular eviction here */
        /* For now, just reject */
        return EKK_ERR_NO_MEMORY;
    }

    /* Find or create stream */
    ekk_stream_state_t *stream = find_stream(index, entry->stream_id);
    if (!stream) {
        stream = create_stream(index, entry->stream_id);
        if (!stream) {
            return EKK_ERR_NO_MEMORY;
        }
    }

    /* Add entry */
    uint32_t idx = index->count;
    index->entries[idx] = *entry;
    index->count++;

    /* Update stream state */
    if (stream->first_index == 0xFFFFFFFF) {
        stream->first_index = idx;
    }
    stream->last_index = idx;
    stream->sequence++;
    stream->event_count++;
    stream->last_timestamp_us = entry->timestamp_us;

    /* Update global state */
    if (idx == 0 || entry->global_seq < index->oldest_seq) {
        index->oldest_seq = entry->global_seq;
    }
    if (entry->global_seq > index->newest_seq) {
        index->newest_seq = entry->global_seq;
    }

    return EKK_OK;
}

ekk_error_t ekk_stream_index_find_seq(ekk_stream_index_t *index,
                                       uint32_t from_seq,
                                       uint32_t to_seq,
                                       uint32_t *first_idx,
                                       uint32_t *count) {
    if (!index || !first_idx || !count) {
        return EKK_ERR_INVALID_ARG;
    }

    if (index->count == 0) {
        *first_idx = 0;
        *count = 0;
        return EKK_ERR_NOT_FOUND;
    }

    /* Binary search for start */
    uint32_t start = binary_search_seq(index, from_seq);
    if (start >= index->count) {
        *first_idx = 0;
        *count = 0;
        return EKK_ERR_NOT_FOUND;
    }

    /* Linear scan to find end */
    uint32_t end = start;
    while (end < index->count && index->entries[end].global_seq <= to_seq) {
        end++;
    }

    *first_idx = start;
    *count = end - start;

    return (*count > 0) ? EKK_OK : EKK_ERR_NOT_FOUND;
}

ekk_error_t ekk_stream_index_find_time(ekk_stream_index_t *index,
                                        uint32_t from_time_us,
                                        uint32_t to_time_us,
                                        uint32_t *first_idx,
                                        uint32_t *count) {
    if (!index || !first_idx || !count) {
        return EKK_ERR_INVALID_ARG;
    }

    if (index->count == 0) {
        *first_idx = 0;
        *count = 0;
        return EKK_ERR_NOT_FOUND;
    }

    /* Binary search for start time */
    uint32_t start = binary_search_time(index, from_time_us);
    if (start >= index->count) {
        *first_idx = 0;
        *count = 0;
        return EKK_ERR_NOT_FOUND;
    }

    /* Linear scan to find end time */
    uint32_t end = start;
    while (end < index->count && index->entries[end].timestamp_us <= to_time_us) {
        end++;
    }

    *first_idx = start;
    *count = end - start;

    return (*count > 0) ? EKK_OK : EKK_ERR_NOT_FOUND;
}

ekk_error_t ekk_stream_index_find_stream(ekk_stream_index_t *index,
                                          ekk_module_id_t stream_id,
                                          uint32_t from_stream_seq,
                                          uint32_t *first_idx,
                                          uint32_t *count) {
    if (!index || !first_idx || !count) {
        return EKK_ERR_INVALID_ARG;
    }

    *first_idx = 0;
    *count = 0;

    /* Find stream state */
    ekk_stream_state_t *stream = find_stream(index, stream_id);
    if (!stream || stream->first_index == 0xFFFFFFFF) {
        return EKK_ERR_NOT_FOUND;
    }

    /* Scan stream's entries */
    uint32_t found_first = 0xFFFFFFFF;
    uint32_t found_count = 0;

    for (uint32_t i = stream->first_index; i <= stream->last_index && i < index->count; i++) {
        ekk_index_entry_t *entry = &index->entries[i];
        if (entry->stream_id == stream_id) {
            if (entry->stream_seq >= from_stream_seq) {
                if (found_first == 0xFFFFFFFF) {
                    found_first = i;
                }
                found_count++;
            }
        }
    }

    if (found_first == 0xFFFFFFFF) {
        return EKK_ERR_NOT_FOUND;
    }

    *first_idx = found_first;
    *count = found_count;

    return EKK_OK;
}

/* ============================================================================
 * QUERY HELPERS
 * ============================================================================ */

/**
 * @brief Get entry at index
 */
const ekk_index_entry_t *ekk_stream_index_get(ekk_stream_index_t *index,
                                                uint32_t idx) {
    if (!index || idx >= index->count) {
        return NULL;
    }
    return &index->entries[idx];
}

/**
 * @brief Get stream info
 */
ekk_error_t ekk_stream_index_stream_info(ekk_stream_index_t *index,
                                          ekk_module_id_t stream_id,
                                          uint32_t *event_count,
                                          uint32_t *first_seq,
                                          uint32_t *last_seq) {
    if (!index) {
        return EKK_ERR_INVALID_ARG;
    }

    ekk_stream_state_t *stream = find_stream(index, stream_id);
    if (!stream) {
        return EKK_ERR_NOT_FOUND;
    }

    if (event_count) *event_count = stream->event_count;

    if (stream->first_index != 0xFFFFFFFF && first_seq) {
        *first_seq = index->entries[stream->first_index].stream_seq;
    }

    if (stream->last_index != 0xFFFFFFFF && last_seq) {
        *last_seq = index->entries[stream->last_index].stream_seq;
    }

    return EKK_OK;
}

/**
 * @brief Get all active streams
 */
uint32_t ekk_stream_index_list_streams(ekk_stream_index_t *index,
                                         ekk_module_id_t *stream_ids,
                                         uint32_t max_count) {
    if (!index || !stream_ids || max_count == 0) {
        return 0;
    }

    uint32_t count = (index->stream_count < max_count)
                     ? index->stream_count : max_count;

    for (uint32_t i = 0; i < count; i++) {
        stream_ids[i] = index->streams[i].module_id;
    }

    return count;
}

/**
 * @brief Find entries by event type (linear scan)
 */
uint32_t ekk_stream_index_find_by_type(ekk_stream_index_t *index,
                                        uint8_t event_type,
                                        uint32_t *out_indices,
                                        uint32_t max_count) {
    if (!index || !out_indices || max_count == 0) {
        return 0;
    }

    uint32_t found = 0;

    for (uint32_t i = 0; i < index->count && found < max_count; i++) {
        if (index->entries[i].event_type == event_type) {
            out_indices[found++] = i;
        }
    }

    return found;
}

/**
 * @brief Count entries by event type
 */
uint32_t ekk_stream_index_count_by_type(ekk_stream_index_t *index,
                                          uint8_t event_type) {
    if (!index) {
        return 0;
    }

    uint32_t count = 0;

    for (uint32_t i = 0; i < index->count; i++) {
        if (index->entries[i].event_type == event_type) {
            count++;
        }
    }

    return count;
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

/**
 * @brief Get index statistics
 */
void ekk_stream_index_stats(ekk_stream_index_t *index,
                             uint32_t *total_entries,
                             uint32_t *stream_count,
                             uint32_t *capacity_used_pct) {
    if (!index) return;

    if (total_entries) *total_entries = index->count;
    if (stream_count) *stream_count = index->stream_count;
    if (capacity_used_pct) {
        *capacity_used_pct = (index->count * 100) / index->capacity;
    }
}

/**
 * @brief Reset index
 */
void ekk_stream_index_reset(ekk_stream_index_t *index) {
    if (!index) return;

    index->count = 0;
    index->stream_count = 0;
    index->oldest_seq = 0;
    index->newest_seq = 0;

    /* Clear stream states */
    for (uint8_t i = 0; i < EKK_GATEWAY_MAX_MODULES; i++) {
        memset(&index->streams[i], 0, sizeof(ekk_stream_state_t));
    }
}
