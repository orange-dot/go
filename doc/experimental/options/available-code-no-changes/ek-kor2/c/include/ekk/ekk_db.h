/**
 * @file ekk_db.h
 * @brief EK-KOR Database Module Client API
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Provides three database types for EK-KOR modules:
 * - Key-Value Store: configuration, state, metadata
 * - Time-Series DB: telemetry (voltage, current, temperature)
 * - Event/Log DB: structured events with severity, source, timestamps
 *
 * Built on top of EKKFS filesystem (reuses journal, IPC, permissions).
 *
 * Usage:
 *   // Key-Value Store
 *   ekkdb_kv_t kv;
 *   ekkdb_kv_open("config", &kv);
 *   ekkdb_kv_put(&kv, "max_power", "3300", 4);
 *   char val[16];
 *   uint32_t len = sizeof(val);
 *   ekkdb_kv_get(&kv, "max_power", val, &len);
 *   ekkdb_kv_close(&kv);
 *
 *   // Time-Series DB
 *   ekkdb_ts_t ts;
 *   ekkdb_ts_open(42, "power", &ts);
 *   ekkdb_ts_record_t rec = { .timestamp = now_us, .power_mw = 3300000 };
 *   ekkdb_ts_append(&ts, &rec);
 *   ekkdb_ts_close(&ts);
 *
 *   // Event Log
 *   ekkdb_log_t log;
 *   ekkdb_log_open(&log);
 *   ekkdb_event_t evt = { .severity = EKKDB_SEV_INFO, ... };
 *   ekkdb_log_write(&log, &evt);
 *   ekkdb_log_close(&log);
 */

#ifndef EKK_DB_H
#define EKK_DB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Codes
 * ============================================================================ */

#define EKKDB_OK                0
#define EKKDB_ERR_IO            -1      /* Filesystem I/O error */
#define EKKDB_ERR_NOT_FOUND     -2      /* Key/record not found */
#define EKKDB_ERR_EXISTS        -3      /* Key already exists (for create) */
#define EKKDB_ERR_FULL          -4      /* Database full */
#define EKKDB_ERR_INVALID       -5      /* Invalid argument */
#define EKKDB_ERR_NOT_OPEN      -6      /* Database not open */
#define EKKDB_ERR_KEY_TOO_LONG  -7      /* Key exceeds max length */
#define EKKDB_ERR_VALUE_TOO_BIG -8      /* Value exceeds max size */
#define EKKDB_ERR_TIMEOUT       -9      /* IPC timeout */
#define EKKDB_ERR_NOT_READY     -10     /* Server not ready */
#define EKKDB_ERR_CORRUPT       -11     /* Database corrupted */
#define EKKDB_ERR_PERMISSION    -12     /* Permission denied */

/* ============================================================================
 * Key-Value Store Constants
 * ============================================================================ */

#define EKKDB_KV_MAX_KEY_LEN    14      /* Max key length (without null) */
#define EKKDB_KV_MAX_VALUE_LEN  14      /* Max inline value length */
#define EKKDB_KV_MAX_NAMESPACE  12      /* Max namespace length */

/* ============================================================================
 * Time-Series Constants
 * ============================================================================ */

#define EKKDB_TS_MAX_METRIC_LEN 8       /* Max metric name length */

/* ============================================================================
 * Event Log Constants
 * ============================================================================ */

/* Event severity levels */
#define EKKDB_SEV_DEBUG         0
#define EKKDB_SEV_INFO          1
#define EKKDB_SEV_WARN          2
#define EKKDB_SEV_ERROR         3
#define EKKDB_SEV_FATAL         4
#define EKKDB_SEV_ALARM         5

/* Event source types */
#define EKKDB_SRC_MODULE        0       /* EK module */
#define EKKDB_SRC_SYSTEM        1       /* System/kernel */
#define EKKDB_SRC_CONSENSUS     2       /* Consensus algorithm */
#define EKKDB_SRC_FIELD         3       /* Field computation */
#define EKKDB_SRC_NETWORK       4       /* CAN/network */
#define EKKDB_SRC_POWER         5       /* Power electronics */
#define EKKDB_SRC_THERMAL       6       /* Thermal management */
#define EKKDB_SRC_EXTERNAL      7       /* External (V2G, OCPP, etc.) */

/* Event message max length */
#define EKKDB_EVENT_MSG_LEN     32

/* ============================================================================
 * Key-Value Store Types
 * ============================================================================ */

/**
 * @brief Key-Value database handle
 */
typedef struct {
    uint32_t handle;            /* Internal handle from server */
    uint8_t  owner_id;          /* Owner module ID */
    char     namespace_name[EKKDB_KV_MAX_NAMESPACE + 1];
    uint8_t  is_open;           /* 1 if open */
} ekkdb_kv_t;

/* ============================================================================
 * Time-Series Types
 * ============================================================================ */

/**
 * @brief Time-Series record (32 bytes, 16 per 512-byte block)
 */
typedef struct {
    uint64_t timestamp;         /* Microseconds since epoch */
    int32_t  voltage_mv;        /* Voltage in millivolts */
    int32_t  current_ma;        /* Current in milliamps */
    int32_t  temp_mc;           /* Temperature in millicelsius */
    int32_t  power_mw;          /* Power in milliwatts */
    uint16_t flags;             /* Application-defined flags */
    uint16_t module_id;         /* Source module ID */
} __attribute__((packed)) ekkdb_ts_record_t;

/**
 * @brief Time-Series database handle
 */
typedef struct {
    uint32_t handle;            /* Internal handle from server */
    uint8_t  module_id;         /* Module ID */
    char     metric[EKKDB_TS_MAX_METRIC_LEN + 1];
    uint8_t  is_open;           /* 1 if open */
} ekkdb_ts_t;

/**
 * @brief Time-Series query iterator
 */
typedef struct {
    uint32_t handle;            /* Iterator handle from server */
    uint64_t start_us;          /* Query start time */
    uint64_t end_us;            /* Query end time */
    uint32_t current_idx;       /* Current index */
    uint32_t total_count;       /* Total records in range */
    uint8_t  is_valid;          /* 1 if iterator is valid */
} ekkdb_ts_iter_t;

/* ============================================================================
 * Event Log Types
 * ============================================================================ */

/**
 * @brief Event record (64 bytes, 8 per 512-byte block)
 */
typedef struct {
    uint64_t timestamp;         /* Microseconds since epoch */
    uint32_t sequence;          /* Sequence number (monotonic) */
    uint8_t  severity;          /* EKKDB_SEV_* */
    uint8_t  source_type;       /* EKKDB_SRC_* */
    uint8_t  source_id;         /* Source module/subsystem ID */
    uint8_t  event_type;        /* Application-defined event type */
    uint32_t event_code;        /* Application-defined event code */
    uint32_t param1;            /* Event parameter 1 */
    uint32_t param2;            /* Event parameter 2 */
    char     message[EKKDB_EVENT_MSG_LEN];  /* Human-readable message */
    uint32_t crc32;             /* CRC32 for integrity */
} __attribute__((packed)) ekkdb_event_t;

/**
 * @brief Event log handle
 */
typedef struct {
    uint32_t handle;            /* Internal handle from server */
    uint8_t  is_open;           /* 1 if open */
} ekkdb_log_t;

/**
 * @brief Event log query filter
 */
typedef struct {
    uint64_t start_us;          /* Start time (0 = no limit) */
    uint64_t end_us;            /* End time (0 = no limit) */
    uint8_t  min_severity;      /* Minimum severity (EKKDB_SEV_*) */
    uint8_t  source_type;       /* Filter by source type (0xFF = any) */
    uint8_t  source_id;         /* Filter by source ID (0xFF = any) */
    uint8_t  reserved;
} ekkdb_log_filter_t;

/**
 * @brief Event log query iterator
 */
typedef struct {
    uint32_t handle;            /* Iterator handle from server */
    ekkdb_log_filter_t filter;  /* Query filter */
    uint32_t current_idx;       /* Current index */
    uint32_t total_count;       /* Total matching events */
    uint8_t  is_valid;          /* 1 if iterator is valid */
} ekkdb_log_iter_t;

/* ============================================================================
 * Key-Value Store API
 * ============================================================================ */

/**
 * @brief Open or create a Key-Value namespace
 *
 * Each namespace is stored in a separate file (kv_<namespace>.dat).
 *
 * @param namespace_name Namespace name (max 12 chars, alphanumeric)
 * @param kv Handle to initialize
 * @return EKKDB_OK on success, negative error code on failure
 */
int ekkdb_kv_open(const char *namespace_name, ekkdb_kv_t *kv);

/**
 * @brief Get a value by key
 *
 * @param kv Open KV handle
 * @param key Key string (max 14 chars)
 * @param value Buffer to receive value
 * @param len In: buffer size, Out: actual value length
 * @return EKKDB_OK on success, EKKDB_ERR_NOT_FOUND if key doesn't exist
 */
int ekkdb_kv_get(ekkdb_kv_t *kv, const char *key, void *value, uint32_t *len);

/**
 * @brief Store a key-value pair
 *
 * Creates the key if it doesn't exist, updates if it does.
 *
 * @param kv Open KV handle
 * @param key Key string (max 14 chars)
 * @param value Value data
 * @param len Value length (max 14 bytes for inline storage)
 * @return EKKDB_OK on success, error code on failure
 */
int ekkdb_kv_put(ekkdb_kv_t *kv, const char *key, const void *value, uint32_t len);

/**
 * @brief Delete a key
 *
 * @param kv Open KV handle
 * @param key Key to delete
 * @return EKKDB_OK on success, EKKDB_ERR_NOT_FOUND if key doesn't exist
 */
int ekkdb_kv_delete(ekkdb_kv_t *kv, const char *key);

/**
 * @brief Close the Key-Value database
 *
 * @param kv Handle to close
 * @return EKKDB_OK on success
 */
int ekkdb_kv_close(ekkdb_kv_t *kv);

/**
 * @brief Get number of entries in the KV store
 *
 * @param kv Open KV handle
 * @return Number of entries, or negative error code
 */
int ekkdb_kv_count(ekkdb_kv_t *kv);

/* ============================================================================
 * Time-Series API
 * ============================================================================ */

/**
 * @brief Open or create a Time-Series database
 *
 * Each metric for each module is stored separately (ts_mod<id>_<metric>.dat).
 *
 * @param module_id Module ID (0 for system-wide)
 * @param metric Metric name (max 8 chars: "power", "temp", etc.)
 * @param ts Handle to initialize
 * @return EKKDB_OK on success
 */
int ekkdb_ts_open(uint8_t module_id, const char *metric, ekkdb_ts_t *ts);

/**
 * @brief Append a record to the time-series
 *
 * Records are stored in a ring buffer. When full, oldest records are overwritten.
 *
 * @param ts Open TS handle
 * @param record Record to append (timestamp should be set by caller)
 * @return EKKDB_OK on success
 */
int ekkdb_ts_append(ekkdb_ts_t *ts, const ekkdb_ts_record_t *record);

/**
 * @brief Query time-series records by time range
 *
 * Returns an iterator over records in the specified time range.
 *
 * @param ts Open TS handle
 * @param start_us Start time (microseconds, 0 = oldest)
 * @param end_us End time (microseconds, 0 = newest)
 * @param iter Iterator to initialize
 * @return EKKDB_OK on success
 */
int ekkdb_ts_query(ekkdb_ts_t *ts, uint64_t start_us, uint64_t end_us, ekkdb_ts_iter_t *iter);

/**
 * @brief Get next record from query iterator
 *
 * @param iter Query iterator
 * @param record Buffer to receive record
 * @return EKKDB_OK on success, EKKDB_ERR_NOT_FOUND when no more records
 */
int ekkdb_ts_next(ekkdb_ts_iter_t *iter, ekkdb_ts_record_t *record);

/**
 * @brief Close the iterator
 *
 * @param iter Iterator to close
 * @return EKKDB_OK on success
 */
int ekkdb_ts_iter_close(ekkdb_ts_iter_t *iter);

/**
 * @brief Compact the time-series database
 *
 * Calculates averages over time intervals and writes to a separate file.
 * Frees space in the main ring buffer.
 *
 * @param ts Open TS handle
 * @return EKKDB_OK on success
 */
int ekkdb_ts_compact(ekkdb_ts_t *ts);

/**
 * @brief Close the Time-Series database
 *
 * @param ts Handle to close
 * @return EKKDB_OK on success
 */
int ekkdb_ts_close(ekkdb_ts_t *ts);

/**
 * @brief Get number of records in the time-series
 *
 * @param ts Open TS handle
 * @return Number of records, or negative error code
 */
int ekkdb_ts_count(ekkdb_ts_t *ts);

/* ============================================================================
 * Event Log API
 * ============================================================================ */

/**
 * @brief Open the system event log
 *
 * There is one system-wide event log (log_events.dat).
 *
 * @param log Handle to initialize
 * @return EKKDB_OK on success
 */
int ekkdb_log_open(ekkdb_log_t *log);

/**
 * @brief Write an event to the log
 *
 * The event's timestamp and sequence number are set automatically if zero.
 * CRC32 is calculated automatically.
 *
 * @param log Open log handle
 * @param event Event to write
 * @return EKKDB_OK on success
 */
int ekkdb_log_write(ekkdb_log_t *log, const ekkdb_event_t *event);

/**
 * @brief Query events with a filter
 *
 * Returns an iterator over events matching the filter criteria.
 *
 * @param log Open log handle
 * @param filter Query filter (NULL for all events)
 * @param iter Iterator to initialize
 * @return EKKDB_OK on success
 */
int ekkdb_log_query(ekkdb_log_t *log, const ekkdb_log_filter_t *filter, ekkdb_log_iter_t *iter);

/**
 * @brief Get next event from query iterator
 *
 * @param iter Query iterator
 * @param event Buffer to receive event
 * @return EKKDB_OK on success, EKKDB_ERR_NOT_FOUND when no more events
 */
int ekkdb_log_next(ekkdb_log_iter_t *iter, ekkdb_event_t *event);

/**
 * @brief Close the iterator
 *
 * @param iter Iterator to close
 * @return EKKDB_OK on success
 */
int ekkdb_log_iter_close(ekkdb_log_iter_t *iter);

/**
 * @brief Close the event log
 *
 * @param log Handle to close
 * @return EKKDB_OK on success
 */
int ekkdb_log_close(ekkdb_log_t *log);

/**
 * @brief Get number of events in the log
 *
 * @param log Open log handle
 * @return Number of events, or negative error code
 */
int ekkdb_log_count(ekkdb_log_t *log);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Check if database server is ready
 *
 * @return 1 if ready, 0 if not
 */
int ekkdb_is_ready(void);

/**
 * @brief Helper: Create a simple event with message
 *
 * Initializes an event structure with the given parameters.
 * Sets timestamp to current time, calculates CRC.
 *
 * @param event Event to initialize
 * @param severity Severity level (EKKDB_SEV_*)
 * @param source_type Source type (EKKDB_SRC_*)
 * @param source_id Source module ID
 * @param event_code Application-defined event code
 * @param message Human-readable message (will be truncated if too long)
 */
void ekkdb_event_init(ekkdb_event_t *event, uint8_t severity, uint8_t source_type,
                      uint8_t source_id, uint32_t event_code, const char *message);

/**
 * @brief Helper: Create a time-series record with current timestamp
 *
 * @param record Record to initialize
 * @param module_id Module ID
 * @param voltage_mv Voltage in millivolts
 * @param current_ma Current in milliamps
 * @param temp_mc Temperature in millicelsius
 * @param power_mw Power in milliwatts
 */
void ekkdb_ts_record_init(ekkdb_ts_record_t *record, uint16_t module_id,
                          int32_t voltage_mv, int32_t current_ma,
                          int32_t temp_mc, int32_t power_mw);

#ifdef __cplusplus
}
#endif

#endif /* EKK_DB_H */
