/**
 * @file ekkdb.h
 * @brief EKKDB - EK-KOR Database Internal Structures
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Internal header for database module implementation.
 * Not for use by application code - use ekk_db.h instead.
 */

#ifndef EKKDB_H
#define EKKDB_H

#include "ekk/ekk_db.h"
#include "ekkfs.h"
#include <stdint.h>

/* ============================================================================
 * Configuration (Platform-Specific)
 * ============================================================================ */

#ifdef EKK_PLATFORM_STM32G474
    /* STM32G474: Limited RAM */
    #define EKKDB_KV_MAX_ENTRIES    64      /* Max KV entries per namespace */
    #define EKKDB_TS_MAX_RECORDS    256     /* Max time-series records */
    #define EKKDB_LOG_MAX_EVENTS    128     /* Max events in log */
    #define EKKDB_MAX_KV_HANDLES    2       /* Max open KV handles */
    #define EKKDB_MAX_TS_HANDLES    4       /* Max open TS handles */
#else
    /* RPi3 and other platforms: More RAM available */
    #define EKKDB_KV_MAX_ENTRIES    256     /* Max KV entries per namespace */
    #define EKKDB_TS_MAX_RECORDS    1024    /* Max time-series records */
    #define EKKDB_LOG_MAX_EVENTS    512     /* Max events in log */
    #define EKKDB_MAX_KV_HANDLES    4       /* Max open KV handles */
    #define EKKDB_MAX_TS_HANDLES    8       /* Max open TS handles */
#endif

/* Common constants */
#define EKKDB_BLOCK_SIZE        512         /* Same as EKKFS */
#define EKKDB_COMPACT_INTERVAL  64          /* Records between compaction checks */

/* ============================================================================
 * On-Disk Structures
 * ============================================================================ */

/* Database file magic numbers */
#define EKKDB_KV_MAGIC          0x454B4B56  /* "EKKV" */
#define EKKDB_TS_MAGIC          0x454B5453  /* "EKTS" */
#define EKKDB_LOG_MAGIC         0x454B4C47  /* "EKLG" */

/**
 * @brief Key-Value entry on disk (32 bytes, 16 per block)
 */
typedef struct {
    char     key[15];           /* Null-terminated key */
    uint8_t  flags;             /* 0=free, 1=used, 2=deleted */
    uint8_t  value_len;         /* Value length (0-14 for inline) */
    uint8_t  value[14];         /* Inline value */
    uint8_t  reserved;          /* Padding */
} __attribute__((packed)) ekkdb_kv_entry_t;

#define EKKDB_KV_ENTRY_SIZE     32
#define EKKDB_KV_ENTRIES_PER_BLOCK  (EKKDB_BLOCK_SIZE / EKKDB_KV_ENTRY_SIZE)

/* Entry flags */
#define EKKDB_KV_FLAG_FREE      0
#define EKKDB_KV_FLAG_USED      1
#define EKKDB_KV_FLAG_DELETED   2

/**
 * @brief Key-Value file header (64 bytes)
 */
typedef struct {
    uint32_t magic;             /* EKKDB_KV_MAGIC */
    uint32_t version;           /* Header version (1) */
    uint32_t entry_count;       /* Number of used entries */
    uint32_t max_entries;       /* Max entries (based on file size) */
    uint8_t  owner_id;          /* Owner module ID */
    uint8_t  reserved[3];
    uint64_t created;           /* Creation timestamp */
    uint64_t modified;          /* Last modification timestamp */
    char     namespace_name[16];/* Namespace name */
    uint32_t crc32;             /* Header CRC */
    uint8_t  padding[12];       /* Pad to 64 bytes */
} __attribute__((packed)) ekkdb_kv_header_t;

/**
 * @brief Time-Series file header (64 bytes)
 */
typedef struct {
    uint32_t magic;             /* EKKDB_TS_MAGIC */
    uint32_t version;           /* Header version (1) */
    uint32_t head;              /* Next write index */
    uint32_t tail;              /* Oldest valid index */
    uint32_t count;             /* Number of records */
    uint32_t max_records;       /* Max records (ring buffer size) */
    uint8_t  module_id;         /* Module ID */
    uint8_t  reserved[3];
    char     metric[12];        /* Metric name */
    uint64_t oldest_timestamp;  /* Oldest record timestamp */
    uint64_t newest_timestamp;  /* Newest record timestamp */
    uint32_t crc32;             /* Header CRC */
} __attribute__((packed)) ekkdb_ts_header_t;

#define EKKDB_TS_RECORD_SIZE        32
#define EKKDB_TS_RECORDS_PER_BLOCK  (EKKDB_BLOCK_SIZE / EKKDB_TS_RECORD_SIZE)

/**
 * @brief Event log file header (64 bytes)
 */
typedef struct {
    uint32_t magic;             /* EKKDB_LOG_MAGIC */
    uint32_t version;           /* Header version (1) */
    uint32_t head;              /* Next write index */
    uint32_t tail;              /* Oldest valid index */
    uint32_t count;             /* Number of events */
    uint32_t max_events;        /* Max events (ring buffer size) */
    uint32_t next_sequence;     /* Next sequence number */
    uint32_t reserved;
    uint64_t oldest_timestamp;  /* Oldest event timestamp */
    uint64_t newest_timestamp;  /* Newest event timestamp */
    uint32_t crc32;             /* Header CRC */
    uint8_t  padding[12];       /* Pad to 64 bytes */
} __attribute__((packed)) ekkdb_log_header_t;

#define EKKDB_EVENT_SIZE            64
#define EKKDB_EVENTS_PER_BLOCK      (EKKDB_BLOCK_SIZE / EKKDB_EVENT_SIZE)

/* ============================================================================
 * Runtime Structures
 * ============================================================================ */

/**
 * @brief Internal KV handle state
 */
typedef struct {
    int               in_use;           /* 1 if handle is allocated */
    uint8_t           owner_id;         /* Owner module ID */
    char              namespace_name[EKKDB_KV_MAX_NAMESPACE + 1];
    char              filename[16];     /* kv_<namespace>.dat */
    ekkdb_kv_header_t header;           /* Cached header */
    int               dirty;            /* 1 if header needs write */
} ekkdb_kv_state_t;

/**
 * @brief Internal TS handle state
 */
typedef struct {
    int               in_use;           /* 1 if handle is allocated */
    uint8_t           module_id;        /* Module ID */
    char              metric[EKKDB_TS_MAX_METRIC_LEN + 1];
    char              filename[16];     /* ts_mod<id>_<metric>.dat */
    ekkdb_ts_header_t header;           /* Cached header */
    int               dirty;            /* 1 if header needs write */
} ekkdb_ts_state_t;

/**
 * @brief Internal log handle state
 */
typedef struct {
    int                in_use;          /* 1 if handle is allocated */
    char               filename[16];    /* log_events.dat */
    ekkdb_log_header_t header;          /* Cached header */
    int                dirty;           /* 1 if header needs write */
} ekkdb_log_state_t;

/**
 * @brief TS query iterator state
 */
typedef struct {
    int               in_use;           /* 1 if iterator is allocated */
    uint32_t          ts_handle;        /* Parent TS handle */
    uint64_t          start_us;         /* Query start time */
    uint64_t          end_us;           /* Query end time */
    uint32_t          current_idx;      /* Current record index in ring */
    uint32_t          records_returned; /* Count of records returned */
} ekkdb_ts_iter_state_t;

/**
 * @brief Log query iterator state
 */
typedef struct {
    int                in_use;          /* 1 if iterator is allocated */
    ekkdb_log_filter_t filter;          /* Query filter */
    uint32_t           current_idx;     /* Current event index in ring */
    uint32_t           events_returned; /* Count of events returned */
} ekkdb_log_iter_state_t;

/* ============================================================================
 * IPC Message Types (extend fs_server.h)
 * ============================================================================ */

/* Database IPC commands (start at 0x40 to avoid collision with FS) */
#define EKKDB_IPC_KV_OPEN       0x40
#define EKKDB_IPC_KV_CLOSE      0x41
#define EKKDB_IPC_KV_GET        0x42
#define EKKDB_IPC_KV_PUT        0x43
#define EKKDB_IPC_KV_DELETE     0x44
#define EKKDB_IPC_KV_COUNT      0x45

#define EKKDB_IPC_TS_OPEN       0x50
#define EKKDB_IPC_TS_CLOSE      0x51
#define EKKDB_IPC_TS_APPEND     0x52
#define EKKDB_IPC_TS_QUERY      0x53
#define EKKDB_IPC_TS_NEXT       0x54
#define EKKDB_IPC_TS_COMPACT    0x55
#define EKKDB_IPC_TS_COUNT      0x56
#define EKKDB_IPC_TS_ITER_CLOSE 0x57

#define EKKDB_IPC_LOG_OPEN      0x60
#define EKKDB_IPC_LOG_CLOSE     0x61
#define EKKDB_IPC_LOG_WRITE     0x62
#define EKKDB_IPC_LOG_QUERY     0x63
#define EKKDB_IPC_LOG_NEXT      0x64
#define EKKDB_IPC_LOG_COUNT     0x65
#define EKKDB_IPC_LOG_ITER_CLOSE 0x66

/* Message type for DB requests */
#define MSG_TYPE_DB_REQUEST     0x30
#define MSG_TYPE_DB_RESPONSE    0x31

/**
 * @brief DB Request Message (64 bytes max)
 */
typedef struct {
    uint8_t  cmd;               /* EKKDB_IPC_* command */
    uint8_t  sender_id;         /* Sender's module ID */
    uint16_t req_id;            /* Request ID for matching responses */
    uint32_t handle;            /* Handle (KV, TS, or Log) */
    uint32_t param1;            /* Command-specific parameter */
    uint32_t param2;            /* Command-specific parameter */
    uint8_t  data[48];          /* Inline data (key, namespace, etc.) */
} __attribute__((packed)) ekkdb_request_t;

/**
 * @brief DB Response Message (64 bytes max)
 */
typedef struct {
    uint8_t  status;            /* 0 = OK, else error code */
    uint8_t  cmd;               /* Echo of request command */
    uint16_t req_id;            /* Echo of request ID */
    uint32_t result;            /* Result value (handle, count, etc.) */
    uint8_t  data[56];          /* Response data */
} __attribute__((packed)) ekkdb_response_t;

/* ============================================================================
 * Internal Functions (Server-Side)
 * ============================================================================ */

/**
 * @brief Initialize the database subsystem
 *
 * Called by fs_server during init.
 *
 * @return 0 on success
 */
int ekkdb_init(void);

/**
 * @brief Process a database request
 *
 * Called by fs_server for DB IPC messages.
 *
 * @param req Request message
 * @param resp Response message (filled in)
 */
void ekkdb_handle_request(const ekkdb_request_t *req, ekkdb_response_t *resp);

/* ============================================================================
 * Internal Key-Value Functions
 * ============================================================================ */

/**
 * @brief Server-side KV open
 */
int ekkdb_kv_server_open(const char *namespace_name, uint8_t owner_id);

/**
 * @brief Server-side KV close
 */
int ekkdb_kv_server_close(uint32_t handle);

/**
 * @brief Server-side KV get
 */
int ekkdb_kv_server_get(uint32_t handle, const char *key, void *value, uint32_t *len);

/**
 * @brief Server-side KV put
 */
int ekkdb_kv_server_put(uint32_t handle, const char *key, const void *value, uint32_t len);

/**
 * @brief Server-side KV delete
 */
int ekkdb_kv_server_delete(uint32_t handle, const char *key);

/**
 * @brief Server-side KV count
 */
int ekkdb_kv_server_count(uint32_t handle);

/* ============================================================================
 * Internal Time-Series Functions
 * ============================================================================ */

/**
 * @brief Server-side TS open
 */
int ekkdb_ts_server_open(uint8_t module_id, const char *metric, uint8_t owner_id);

/**
 * @brief Server-side TS close
 */
int ekkdb_ts_server_close(uint32_t handle);

/**
 * @brief Server-side TS append
 */
int ekkdb_ts_server_append(uint32_t handle, const ekkdb_ts_record_t *record);

/**
 * @brief Server-side TS query (create iterator)
 */
int ekkdb_ts_server_query(uint32_t handle, uint64_t start_us, uint64_t end_us,
                          uint32_t *iter_handle, uint32_t *total_count);

/**
 * @brief Server-side TS iterator next
 */
int ekkdb_ts_server_next(uint32_t iter_handle, ekkdb_ts_record_t *record);

/**
 * @brief Server-side TS iterator close
 */
int ekkdb_ts_server_iter_close(uint32_t iter_handle);

/**
 * @brief Server-side TS compact
 */
int ekkdb_ts_server_compact(uint32_t handle);

/**
 * @brief Server-side TS count
 */
int ekkdb_ts_server_count(uint32_t handle);

/* ============================================================================
 * Internal Event Log Functions
 * ============================================================================ */

/**
 * @brief Server-side log open
 */
int ekkdb_log_server_open(uint8_t owner_id);

/**
 * @brief Server-side log close
 */
int ekkdb_log_server_close(uint32_t handle);

/**
 * @brief Server-side log write
 */
int ekkdb_log_server_write(uint32_t handle, const ekkdb_event_t *event);

/**
 * @brief Server-side log query (create iterator)
 */
int ekkdb_log_server_query(uint32_t handle, const ekkdb_log_filter_t *filter,
                           uint32_t *iter_handle, uint32_t *total_count);

/**
 * @brief Server-side log iterator next
 */
int ekkdb_log_server_next(uint32_t iter_handle, ekkdb_event_t *event);

/**
 * @brief Server-side log iterator close
 */
int ekkdb_log_server_iter_close(uint32_t iter_handle);

/**
 * @brief Server-side log count
 */
int ekkdb_log_server_count(uint32_t handle);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Simple hash function for KV keys
 */
uint32_t ekkdb_hash(const char *key);

/**
 * @brief Calculate header CRC
 */
uint32_t ekkdb_header_crc(const void *header, size_t size);

#endif /* EKKDB_H */
