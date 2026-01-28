/**
 * @file ekk_gateway.h
 * @brief EK-KOR v2 - Gateway Event Store for AURIX TC397
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Gateway event store that aggregates events from multiple EK3 modules.
 * Designed for AURIX TC397 platform with 16MB Flash and 2.9MB SRAM.
 *
 * FEATURES:
 * - Append-only event log with Flash wear-leveling
 * - In-memory index for O(log n) queries
 * - Per-module stream tracking
 * - Projection maintenance (module state)
 * - Optional upstream sync to cloud
 */

#ifndef EKK_GATEWAY_H
#define EKK_GATEWAY_H

#include "ekk_types.h"
#include "ekk_gossip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/** Maximum modules the gateway can track */
#ifndef EKK_GATEWAY_MAX_MODULES
#define EKK_GATEWAY_MAX_MODULES     256
#endif

/** Flash sector size (TC397 PFLASH) */
#ifndef EKK_FLASH_SECTOR_SIZE
#define EKK_FLASH_SECTOR_SIZE       16384   /* 16KB */
#endif

/** Maximum events in flash log */
#ifndef EKK_FLASH_LOG_CAPACITY
#define EKK_FLASH_LOG_CAPACITY      262144  /* 256K events */
#endif

/** Maximum entries in memory index */
#ifndef EKK_INDEX_CAPACITY
#define EKK_INDEX_CAPACITY          180000  /* ~2.88MB at 16 bytes/entry */
#endif

/** Number of CAN interfaces */
#ifndef EKK_CAN_INTERFACES
#define EKK_CAN_INTERFACES          12
#endif

/* ============================================================================
 * FLASH LOG ENTRY
 * ============================================================================ */

/**
 * @brief Flash log entry (64 bytes, aligned for Flash programming)
 */
EKK_PACK_BEGIN
typedef struct EKK_PACKED {
    uint32_t global_seq;            /**< Global sequence number */
    uint32_t crc32;                 /**< CRC32 for integrity */
    uint16_t length;                /**< Payload length */
    uint8_t  stream_id;             /**< Module ID (stream = module) */
    uint8_t  event_type;            /**< Event type code */
    uint32_t timestamp_us;          /**< Event timestamp */
    uint32_t origin_seq;            /**< Sequence at origin module */
    uint8_t  data[44];              /**< Event payload */
} ekk_flash_entry_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_flash_entry_t) == 64, "Flash entry must be 64 bytes");

/* ============================================================================
 * INDEX ENTRY
 * ============================================================================ */

/**
 * @brief In-memory index entry (16 bytes)
 *
 * Stored in SRAM for fast queries. Points to flash location.
 */
typedef struct {
    uint32_t global_seq;            /**< Global sequence number */
    uint32_t flash_offset;          /**< Offset in flash log */
    uint32_t timestamp_us;          /**< Event timestamp */
    uint16_t stream_seq;            /**< Per-stream sequence */
    uint8_t  stream_id;             /**< Module ID */
    uint8_t  event_type;            /**< Event type code */
} ekk_index_entry_t;

EKK_STATIC_ASSERT(sizeof(ekk_index_entry_t) == 16, "Index entry must be 16 bytes");

/* ============================================================================
 * STREAM STATE
 * ============================================================================ */

/**
 * @brief Per-stream (per-module) state tracking
 */
typedef struct {
    ekk_module_id_t module_id;      /**< Module ID */
    uint32_t sequence;              /**< Next expected sequence */
    uint32_t event_count;           /**< Total events from this module */
    uint32_t first_index;           /**< First index entry for this stream */
    uint32_t last_index;            /**< Last index entry for this stream */
    uint32_t last_timestamp_us;     /**< Timestamp of last event */
} ekk_stream_state_t;

/* ============================================================================
 * MODULE PROJECTION
 * ============================================================================ */

/**
 * @brief Projected module state (computed from events)
 */
typedef struct {
    ekk_module_id_t module_id;      /**< Module ID */
    ekk_module_state_t state;       /**< Current state */
    ekk_time_us_t last_seen;        /**< Last event timestamp */
    uint32_t event_count;           /**< Total events */

    /* Field values (from FieldPublished events) */
    ekk_fixed_t load;               /**< Load field */
    ekk_fixed_t thermal;            /**< Thermal field */
    ekk_fixed_t power;              /**< Power field */

    /* Health info */
    uint8_t neighbor_count;         /**< Known neighbors */
    uint8_t missed_heartbeats;      /**< Consecutive missed */
    ekk_capability_t capabilities;  /**< Module capabilities */
} ekk_module_projection_t;

/* ============================================================================
 * FLASH LOG
 * ============================================================================ */

/**
 * @brief Flash log with wear-leveling
 */
typedef struct {
    uint32_t base_address;          /**< Flash base address */
    uint32_t capacity;              /**< Maximum entries */
    uint32_t sector_count;          /**< Number of sectors */
    uint32_t write_head;            /**< Current write position */
    uint32_t write_sector;          /**< Current write sector */
    uint32_t global_sequence;       /**< Next global sequence */
    uint32_t total_writes;          /**< Total writes (lifetime) */
    uint32_t total_erases;          /**< Total erases (lifetime) */
    uint32_t crc_errors;            /**< CRC errors detected */
} ekk_flash_log_t;

/* ============================================================================
 * STREAM INDEX
 * ============================================================================ */

/**
 * @brief In-memory stream index
 */
typedef struct {
    ekk_index_entry_t *entries;     /**< Index entry array (in SRAM) */
    uint32_t capacity;              /**< Maximum entries */
    uint32_t count;                 /**< Current entry count */
    uint32_t oldest_seq;            /**< Oldest sequence in index */
    uint32_t newest_seq;            /**< Newest sequence in index */

    /* Per-stream state */
    ekk_stream_state_t streams[EKK_GATEWAY_MAX_MODULES];
    uint8_t stream_count;
} ekk_stream_index_t;

/* ============================================================================
 * CAN INTERFACE
 * ============================================================================ */

/**
 * @brief CAN interface state
 */
typedef struct {
    uint8_t index;                  /**< Interface index (0-11) */
    bool enabled;                   /**< Interface enabled */
    uint32_t rx_count;              /**< Messages received */
    uint32_t tx_count;              /**< Messages transmitted */
    uint32_t error_count;           /**< Error frames */
    ekk_time_us_t last_activity;    /**< Last message timestamp */
} ekk_can_interface_t;

/* ============================================================================
 * UPSTREAM SYNC
 * ============================================================================ */

/**
 * @brief Upstream sync state (Ethernet to cloud)
 */
typedef struct {
    bool connected;                 /**< Connection active */
    uint32_t sync_cursor;           /**< Last synced sequence */
    uint32_t pending_count;         /**< Events waiting to sync */
    ekk_time_us_t last_sync;        /**< Last successful sync */
    uint32_t sync_errors;           /**< Sync error count */
} ekk_upstream_t;

/* ============================================================================
 * GATEWAY
 * ============================================================================ */

/**
 * @brief Gateway event store
 */
typedef struct {
    /* Identity */
    ekk_module_id_t gateway_id;     /**< Gateway's module ID */
    bool initialized;               /**< Initialization complete */

    /* CAN interfaces */
    ekk_can_interface_t can[EKK_CAN_INTERFACES];
    uint8_t can_count;

    /* Flash log */
    ekk_flash_log_t flash_log;

    /* In-memory index */
    ekk_stream_index_t index;

    /* Module projections */
    ekk_module_projection_t projections[EKK_GATEWAY_MAX_MODULES];
    uint8_t projection_count;

    /* Upstream sync */
    ekk_upstream_t upstream;

    /* Statistics */
    uint32_t events_received;
    uint32_t events_stored;
    uint32_t queries_served;
} ekk_gateway_t;

/* ============================================================================
 * FLASH LOG API
 * ============================================================================ */

/**
 * @brief Initialize flash log
 */
ekk_error_t ekk_flash_log_init(ekk_flash_log_t *log, uint32_t base_address,
                                uint32_t capacity);

/**
 * @brief Write entry to flash log
 * @return Offset of written entry, or 0xFFFFFFFF on error
 */
uint32_t ekk_flash_log_write(ekk_flash_log_t *log, const ekk_flash_entry_t *entry);

/**
 * @brief Read entry from flash log
 */
ekk_error_t ekk_flash_log_read(ekk_flash_log_t *log, uint32_t offset,
                                ekk_flash_entry_t *entry);

/**
 * @brief Verify flash entry CRC
 */
bool ekk_flash_entry_verify(const ekk_flash_entry_t *entry);

/**
 * @brief Compute CRC32 for flash entry
 */
uint32_t ekk_flash_entry_crc(const ekk_flash_entry_t *entry);

/* ============================================================================
 * STREAM INDEX API
 * ============================================================================ */

/**
 * @brief Initialize stream index
 */
ekk_error_t ekk_stream_index_init(ekk_stream_index_t *index,
                                   ekk_index_entry_t *buffer,
                                   uint32_t capacity);

/**
 * @brief Add entry to index
 */
ekk_error_t ekk_stream_index_add(ekk_stream_index_t *index,
                                  const ekk_index_entry_t *entry);

/**
 * @brief Find entries by global sequence range
 */
ekk_error_t ekk_stream_index_find_seq(ekk_stream_index_t *index,
                                       uint32_t from_seq, uint32_t to_seq,
                                       uint32_t *first_idx, uint32_t *count);

/**
 * @brief Find entries by time range
 */
ekk_error_t ekk_stream_index_find_time(ekk_stream_index_t *index,
                                        uint32_t from_time_us, uint32_t to_time_us,
                                        uint32_t *first_idx, uint32_t *count);

/**
 * @brief Find entries by stream (module)
 */
ekk_error_t ekk_stream_index_find_stream(ekk_stream_index_t *index,
                                          ekk_module_id_t stream_id,
                                          uint32_t from_stream_seq,
                                          uint32_t *first_idx, uint32_t *count);

/* ============================================================================
 * GATEWAY API
 * ============================================================================ */

/**
 * @brief Initialize gateway
 */
ekk_error_t ekk_gateway_init(ekk_gateway_t *gw, ekk_module_id_t gateway_id,
                              uint32_t flash_base, ekk_index_entry_t *index_buffer,
                              uint32_t index_capacity);

/**
 * @brief Process incoming CAN message
 */
ekk_error_t ekk_gateway_process_can(ekk_gateway_t *gw, uint8_t can_idx,
                                     const uint8_t *data, uint32_t len);

/**
 * @brief Append event from module
 * @return Global sequence number, or 0 on error
 */
uint32_t ekk_gateway_append(ekk_gateway_t *gw, ekk_module_id_t module_id,
                             const ekk_event_v2_t *event);

/**
 * @brief Read stream (returns count, fills buffer)
 */
uint32_t ekk_gateway_read_stream(ekk_gateway_t *gw, ekk_module_id_t stream_id,
                                  uint32_t from_seq, ekk_event_v2_t *buffer,
                                  uint32_t max_count);

/**
 * @brief Get module projection
 */
ekk_error_t ekk_gateway_module_state(ekk_gateway_t *gw, ekk_module_id_t module_id,
                                      ekk_module_projection_t *projection);

/**
 * @brief Get module state at specific time (temporal query)
 */
ekk_error_t ekk_gateway_state_at(ekk_gateway_t *gw, ekk_module_id_t module_id,
                                  uint32_t timestamp_us,
                                  ekk_module_projection_t *projection);

/**
 * @brief Periodic gateway tick
 */
ekk_error_t ekk_gateway_tick(ekk_gateway_t *gw, ekk_time_us_t now);

/* ============================================================================
 * UPSTREAM SYNC API
 * ============================================================================ */

/**
 * @brief Initialize upstream connection
 */
ekk_error_t ekk_upstream_init(ekk_upstream_t *upstream);

/**
 * @brief Sync pending events to upstream
 */
ekk_error_t ekk_upstream_sync(ekk_gateway_t *gw);

/**
 * @brief Pull events from upstream (recovery)
 */
ekk_error_t ekk_upstream_pull(ekk_gateway_t *gw, uint32_t from_seq);

#ifdef __cplusplus
}
#endif

#endif /* EKK_GATEWAY_H */
