/**
 * @file fs_server.h
 * @brief EKKFS Server Module for EK-KOR2 Microkernel
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * The FS server runs on Core 3 and provides filesystem services
 * to other cores via IPC message passing.
 */

#ifndef RPI3_FS_SERVER_H
#define RPI3_FS_SERVER_H

#include <stdint.h>

/* ============================================================================
 * IPC Message Types for Filesystem
 * ============================================================================ */

/* Request commands */
#define EKKFS_IPC_OPEN          1
#define EKKFS_IPC_CLOSE         2
#define EKKFS_IPC_READ          3
#define EKKFS_IPC_WRITE         4
#define EKKFS_IPC_CREATE        5
#define EKKFS_IPC_DELETE        6
#define EKKFS_IPC_LIST          7
#define EKKFS_IPC_STAT          8
#define EKKFS_IPC_SEEK          9
#define EKKFS_IPC_SYNC          10
#define EKKFS_IPC_STATFS        11

/* Message type for FS requests (used in msg_queue) */
#define MSG_TYPE_FS_REQUEST     0x20
#define MSG_TYPE_FS_RESPONSE    0x21

/* ============================================================================
 * IPC Message Structures
 * ============================================================================ */

/**
 * @brief FS Request Message (64 bytes max)
 */
typedef struct {
    uint8_t  cmd;               /* EKKFS_IPC_* command */
    uint8_t  sender_id;         /* Sender's module ID */
    uint16_t req_id;            /* Request ID for matching responses */
    uint32_t inode;             /* Inode number (for open files) */
    uint32_t offset;            /* File offset or position */
    uint32_t length;            /* Length for read/write */
    uint8_t  data[48];          /* Inline data or filename */
} __attribute__((packed)) ekkfs_request_t;

/**
 * @brief FS Response Message (64 bytes max)
 */
typedef struct {
    uint8_t  status;            /* 0 = OK, else EKKFS error code */
    uint8_t  cmd;               /* Echo of request command */
    uint16_t req_id;            /* Echo of request ID */
    uint32_t result;            /* Bytes read/written, inode number, etc. */
    uint8_t  data[56];          /* Response data */
} __attribute__((packed)) ekkfs_response_t;

/* ============================================================================
 * FS Server Functions
 * ============================================================================ */

/**
 * @brief Initialize the FS server
 *
 * Initializes the SD card, mounts EKKFS, and prepares for requests.
 * Called by Core 3 during startup.
 *
 * @return 0 on success, negative error code on failure
 */
int fs_server_init(void);

/**
 * @brief Main FS server loop
 *
 * Processes incoming IPC requests from other cores.
 * This function does not return.
 */
void fs_server_main(void);

/**
 * @brief Process a single FS request
 *
 * Called by the server main loop for each incoming request.
 *
 * @param req Request message
 * @param resp Response message (filled in)
 */
void fs_server_handle_request(const ekkfs_request_t *req, ekkfs_response_t *resp);

/**
 * @brief Check if FS server is ready
 *
 * @return 1 if ready to accept requests, 0 if not
 */
int fs_server_is_ready(void);

/**
 * @brief Get FS server core ID
 *
 * @return Core ID where FS server runs (3)
 */
uint32_t fs_server_get_core(void);

/* ============================================================================
 * Configuration System
 * ============================================================================ */

#define CONFIG_MAX_ENTRIES      32      /* Maximum config entries */
#define CONFIG_KEY_MAX_LEN      31      /* Max key length (+ null) */
#define CONFIG_VALUE_MAX_LEN    63      /* Max value length (+ null) */
#define CONFIG_FILE_NAME        "system.cfg"  /* Config file name */

/**
 * @brief Config entry structure
 */
typedef struct {
    char key[CONFIG_KEY_MAX_LEN + 1];
    char value[CONFIG_VALUE_MAX_LEN + 1];
} config_entry_t;

/**
 * @brief Load configuration from file
 *
 * Reads system.cfg and parses key=value pairs.
 * Called automatically during fs_server_init().
 *
 * @return Number of config entries loaded, or negative error
 */
int config_load(void);

/**
 * @brief Save configuration to file
 *
 * Writes all config entries to system.cfg.
 *
 * @return 0 on success, negative error
 */
int config_save(void);

/**
 * @brief Get a config value as string
 *
 * @param key Config key
 * @param default_val Default value if key not found
 * @return Config value or default_val
 */
const char* config_get_str(const char *key, const char *default_val);

/**
 * @brief Get a config value as integer
 *
 * @param key Config key
 * @param default_val Default value if key not found
 * @return Config value or default_val
 */
int config_get_int(const char *key, int default_val);

/**
 * @brief Set a config value
 *
 * @param key Config key
 * @param value Config value
 * @return 0 on success, -1 if full or invalid
 */
int config_set(const char *key, const char *value);

/**
 * @brief Set a config value as integer
 *
 * @param key Config key
 * @param value Integer value
 * @return 0 on success, -1 if full or invalid
 */
int config_set_int(const char *key, int value);

/**
 * @brief Get number of config entries
 *
 * @return Number of entries
 */
int config_count(void);

/* ============================================================================
 * Logging System with Rotation
 * ============================================================================ */

#define LOG_DEFAULT_FILE        "system.log"    /* Default log file */
#define LOG_MAX_SIZE            4096            /* Max log size before rotation */
#define LOG_MAX_ROTATIONS       3               /* Keep N old log files */
#define LOG_BUFFER_SIZE         256             /* Internal line buffer size */

/**
 * @brief Log levels
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

/**
 * @brief Initialize the logging system
 *
 * Opens or creates the log file.
 *
 * @param filename Log file name (or NULL for default)
 * @param max_size Maximum log size before rotation (0 = default)
 * @param max_rotations Maximum number of rotation files (0 = default)
 * @return 0 on success
 */
int syslog_init(const char *filename, uint32_t max_size, int max_rotations);

/**
 * @brief Write a log message
 *
 * Automatically rotates if file exceeds max_size.
 *
 * @param level Log level
 * @param format Printf-style format string
 * @param ... Arguments
 * @return Number of bytes written, or negative error
 */
int syslog_write(log_level_t level, const char *format, ...);

/**
 * @brief Force log rotation
 *
 * Rotates current log to .1, .1 to .2, etc.
 *
 * @return 0 on success
 */
int syslog_rotate(void);

/**
 * @brief Close the logging system
 *
 * Flushes and closes the log file.
 */
void syslog_close(void);

/**
 * @brief Set minimum log level
 *
 * Messages below this level are discarded.
 *
 * @param level Minimum level
 */
void syslog_set_level(log_level_t level);

/**
 * @brief Get current log file size
 *
 * @return Current size in bytes
 */
uint32_t syslog_get_size(void);

#endif /* RPI3_FS_SERVER_H */
