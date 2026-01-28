/**
 * @file fs_server.c
 * @brief EKKFS Server Module Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * This module implements the filesystem server for the EK-KOR2 microkernel.
 * It runs on Core 3 and handles all SD card and filesystem operations.
 * Other cores communicate via IPC message passing.
 */

#include "fs_server.h"
#include "db_server.h"
#include "../../ekkfs.h"
#include "../../ekkdb.h"
#include "sd.h"
#include "msg_queue.h"
#include "uart.h"
#include "smp.h"
#include "rpi3_hw.h"
#include <string.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define FS_SERVER_CORE          3
#define FS_MAX_OPEN_FILES       16

/* ============================================================================
 * State
 * ============================================================================ */

/* Server ready flag (visible to other cores) */
static volatile int g_fs_server_ready = 0;

/* Open file handles */
static ekkfs_file_t g_open_files[FS_MAX_OPEN_FILES];
static int g_open_file_owners[FS_MAX_OPEN_FILES];  /* Owner module ID */

/* ============================================================================
 * File Handle Management
 * ============================================================================ */

static int alloc_file_handle(uint8_t owner_id)
{
    for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (g_open_files[i].inode_num == 0) {
            g_open_file_owners[i] = owner_id;
            return i;
        }
    }
    return -1;
}

static void free_file_handle(int handle)
{
    if (handle >= 0 && handle < FS_MAX_OPEN_FILES) {
        memset(&g_open_files[handle], 0, sizeof(ekkfs_file_t));
        g_open_file_owners[handle] = 0;
    }
}

static ekkfs_file_t* get_file_handle(int handle, uint8_t owner_id)
{
    if (handle < 0 || handle >= FS_MAX_OPEN_FILES) {
        return NULL;
    }
    if (g_open_files[handle].inode_num == 0) {
        return NULL;
    }
    /* Allow system (owner_id=0) to access any file */
    if (owner_id != 0 && g_open_file_owners[handle] != owner_id) {
        return NULL;
    }
    return &g_open_files[handle];
}

/* ============================================================================
 * Request Handlers
 * ============================================================================ */

static void handle_create(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    const char *name = (const char *)req->data;
    uint32_t owner_id = req->sender_id;
    uint32_t flags = req->offset;  /* flags passed in offset field */

    int result = ekkfs_create(name, owner_id, flags);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;  /* inode number */
        uart_printf("FS: Created '%s' (inode %d)\n", name, result);
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
        uart_printf("FS: Create '%s' failed: %d\n", name, result);
    }
}

static void handle_delete(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    const char *name = (const char *)req->data;
    uint32_t owner_id = req->sender_id;

    int result = ekkfs_delete(name, owner_id);
    resp->status = (result == EKKFS_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_open(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    const char *name = (const char *)req->data;

    int handle = alloc_file_handle(req->sender_id);
    if (handle < 0) {
        resp->status = (uint8_t)(-EKKFS_ERR_FULL);
        return;
    }

    int result = ekkfs_open(&g_open_files[handle], name);
    if (result == EKKFS_OK) {
        resp->status = 0;
        resp->result = handle;  /* Return file handle */
        uart_printf("FS: Opened '%s' (handle %d)\n", name, handle);
    } else {
        free_file_handle(handle);
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_close(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    int handle = (int)req->inode;
    ekkfs_file_t *file = get_file_handle(handle, req->sender_id);

    if (file == NULL) {
        resp->status = (uint8_t)(-EKKFS_ERR_INVALID);
        return;
    }

    ekkfs_close(file);
    free_file_handle(handle);
    resp->status = 0;
    resp->result = 0;
}

static void handle_read(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    int handle = (int)req->inode;
    ekkfs_file_t *file = get_file_handle(handle, req->sender_id);

    if (file == NULL) {
        resp->status = (uint8_t)(-EKKFS_ERR_INVALID);
        return;
    }

    /* Limit read to response data buffer size */
    uint32_t length = req->length;
    if (length > sizeof(resp->data)) {
        length = sizeof(resp->data);
    }

    int result = ekkfs_read(file, resp->data, length);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_write(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    int handle = (int)req->inode;
    ekkfs_file_t *file = get_file_handle(handle, req->sender_id);

    if (file == NULL) {
        resp->status = (uint8_t)(-EKKFS_ERR_INVALID);
        return;
    }

    uint32_t length = req->length;
    if (length > sizeof(req->data)) {
        length = sizeof(req->data);
    }

    int result = ekkfs_write(file, req->data, length, req->sender_id);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_seek(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    int handle = (int)req->inode;
    ekkfs_file_t *file = get_file_handle(handle, req->sender_id);

    if (file == NULL) {
        resp->status = (uint8_t)(-EKKFS_ERR_INVALID);
        return;
    }

    ekkfs_seek(file, req->offset);
    resp->status = 0;
    resp->result = file->position;
}

static void handle_stat(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    const char *name = (const char *)req->data;
    ekkfs_stat_t stat;

    int result = ekkfs_stat(name, &stat);
    if (result == EKKFS_OK) {
        resp->status = 0;
        resp->result = stat.size;
        /* Pack stat info into response data */
        memcpy(resp->data, &stat, sizeof(stat));
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_sync(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    (void)req;
    int result = ekkfs_sync();
    resp->status = (result == EKKFS_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_statfs(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    (void)req;
    uint32_t total, free_blks, total_inodes, used_inodes;

    int result = ekkfs_statfs(&total, &free_blks, &total_inodes, &used_inodes);
    if (result == EKKFS_OK) {
        resp->status = 0;
        resp->result = free_blks;
        /* Pack stats into response */
        uint32_t *data32 = (uint32_t *)resp->data;
        data32[0] = total;
        data32[1] = free_blks;
        data32[2] = total_inodes;
        data32[3] = used_inodes;
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

/* ============================================================================
 * Server Main Functions
 * ============================================================================ */

void fs_server_handle_request(const ekkfs_request_t *req, ekkfs_response_t *resp)
{
    /* Initialize response */
    memset(resp, 0, sizeof(*resp));
    resp->cmd = req->cmd;
    resp->req_id = req->req_id;

    switch (req->cmd) {
        case EKKFS_IPC_CREATE:
            handle_create(req, resp);
            break;
        case EKKFS_IPC_DELETE:
            handle_delete(req, resp);
            break;
        case EKKFS_IPC_OPEN:
            handle_open(req, resp);
            break;
        case EKKFS_IPC_CLOSE:
            handle_close(req, resp);
            break;
        case EKKFS_IPC_READ:
            handle_read(req, resp);
            break;
        case EKKFS_IPC_WRITE:
            handle_write(req, resp);
            break;
        case EKKFS_IPC_SEEK:
            handle_seek(req, resp);
            break;
        case EKKFS_IPC_STAT:
            handle_stat(req, resp);
            break;
        case EKKFS_IPC_SYNC:
            handle_sync(req, resp);
            break;
        case EKKFS_IPC_STATFS:
            handle_statfs(req, resp);
            break;
        default:
            resp->status = (uint8_t)(-EKKFS_ERR_INVALID);
            uart_printf("FS: Unknown command %d\n", req->cmd);
            break;
    }
}

int fs_server_init(void)
{
    uart_puts("FS Server: Initializing...\n");

    /* Verify we're on the correct core */
    uint32_t core = smp_get_core_id();
    if (core != FS_SERVER_CORE) {
        uart_printf("FS Server: ERROR - Running on core %d, expected %d\n",
                    core, FS_SERVER_CORE);
        return -1;
    }

    /* Clear file handles */
    memset(g_open_files, 0, sizeof(g_open_files));
    memset(g_open_file_owners, 0, sizeof(g_open_file_owners));

    /* Initialize SD card */
    uart_puts("FS Server: Initializing SD card...\n");
    int result = sd_init();
    if (result != SD_OK) {
        uart_printf("FS Server: SD init failed: %d\n", result);
        return result;
    }

    /* Find EKKFS partition */
    sd_partition_t ekkfs_part;
    result = sd_find_ekkfs_partition(&ekkfs_part);
    if (result != SD_OK) {
        uart_puts("FS Server: EKKFS partition not found, formatting...\n");

        /* Try to parse MBR and use second partition */
        sd_partition_t parts[4];
        if (sd_parse_mbr(parts) == SD_OK && parts[1].is_valid) {
            ekkfs_part = parts[1];
            uart_printf("FS Server: Using partition 1 (LBA %lu, %lu sectors)\n",
                        ekkfs_part.lba_start, ekkfs_part.sector_count);

            /* Format the partition */
            result = ekkfs_format(ekkfs_part.lba_start, ekkfs_part.sector_count, 256);
            if (result != EKKFS_OK) {
                uart_printf("FS Server: Format failed: %d\n", result);
                return result;
            }
        } else {
            uart_puts("FS Server: No usable partition found\n");
            return EKKFS_ERR_NOT_FOUND;
        }
    }

    /* Mount EKKFS */
    uart_printf("FS Server: Mounting EKKFS at LBA %lu...\n", ekkfs_part.lba_start);
    result = ekkfs_mount(ekkfs_part.lba_start);
    if (result != EKKFS_OK) {
        uart_printf("FS Server: Mount failed: %d\n", result);
        return result;
    }

    /* Load configuration */
    config_load();

    /* Initialize DB server */
    result = db_server_init();
    if (result != 0) {
        uart_printf("FS Server: DB init failed: %d\n", result);
        /* Continue anyway - FS is still usable without DB */
    }

    /* Mark server as ready */
    __asm__ volatile("dmb sy" ::: "memory");
    g_fs_server_ready = 1;
    __asm__ volatile("dmb sy" ::: "memory");

    uart_puts("FS Server: Ready\n");
    return 0;
}

void fs_server_main(void)
{
    uart_puts("FS Server: Entering main loop\n");

    /* Union for both FS and DB messages (same size) */
    union {
        ekkfs_request_t fs_req;
        ekkdb_request_t db_req;
    } req;
    union {
        ekkfs_response_t fs_resp;
        ekkdb_response_t db_resp;
    } resp;
    uint8_t sender_id;
    uint8_t msg_type;
    uint32_t len;

    while (1) {
        /* Poll for messages */
        len = sizeof(req);
        if (msg_queue_recv(&sender_id, &msg_type, &req, &len) == 0) {
            if (msg_type == MSG_TYPE_FS_REQUEST) {
                /* Process FS request */
                req.fs_req.sender_id = sender_id;
                fs_server_handle_request(&req.fs_req, &resp.fs_resp);

                /* Send response */
                uint32_t dest_core = sender_id - 1;  /* module ID to core ID */
                if (dest_core < 4) {
                    msg_queue_send(dest_core, FS_SERVER_CORE + 1,
                                   MSG_TYPE_FS_RESPONSE, &resp.fs_resp, sizeof(resp.fs_resp));
                }
            } else if (msg_type == MSG_TYPE_DB_REQUEST) {
                /* Process DB request */
                req.db_req.sender_id = sender_id;
                db_server_handle_request(&req.db_req, &resp.db_resp);

                /* Send response */
                uint32_t dest_core = sender_id - 1;  /* module ID to core ID */
                if (dest_core < 4) {
                    msg_queue_send(dest_core, FS_SERVER_CORE + 1,
                                   MSG_TYPE_DB_RESPONSE, &resp.db_resp, sizeof(resp.db_resp));
                }
            }
        }

        /* Yield CPU briefly */
        __asm__ volatile("yield");
    }
}

int fs_server_is_ready(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
    return g_fs_server_ready;
}

uint32_t fs_server_get_core(void)
{
    return FS_SERVER_CORE;
}

/* ============================================================================
 * Configuration System
 * ============================================================================ */

static config_entry_t g_config[CONFIG_MAX_ENTRIES];
static int g_config_count = 0;

/**
 * @brief Find a config entry by key
 * @return Index or -1 if not found
 */
static int config_find(const char *key)
{
    for (int i = 0; i < g_config_count; i++) {
        if (strcmp(g_config[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Simple atoi implementation
 */
static int simple_atoi(const char *str)
{
    int result = 0;
    int sign = 1;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t') str++;

    /* Handle sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* Parse digits */
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

/**
 * @brief Simple itoa implementation
 */
static void simple_itoa(int value, char *buf, int bufsize)
{
    char tmp[16];
    int i = 0;
    int neg = 0;

    if (value < 0) {
        neg = 1;
        value = -value;
    }

    do {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value && i < 15);

    if (neg) tmp[i++] = '-';

    /* Reverse into buf */
    int j = 0;
    while (i-- > 0 && j < bufsize - 1) {
        buf[j++] = tmp[i];
    }
    buf[j] = '\0';
}

int config_load(void)
{
    uart_puts("Config: Loading system.cfg...\n");

    /* Clear existing config */
    g_config_count = 0;
    memset(g_config, 0, sizeof(g_config));

    /* Open config file */
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, CONFIG_FILE_NAME);
    if (ret != EKKFS_OK) {
        uart_puts("Config: system.cfg not found, using defaults\n");
        return 0;  /* Not an error - just no config file */
    }

    /* Read file contents */
    char buf[512];
    int n = ekkfs_read(&file, buf, sizeof(buf) - 1);
    ekkfs_close(&file);

    if (n <= 0) {
        uart_puts("Config: Empty config file\n");
        return 0;
    }
    buf[n] = '\0';

    /* Parse key=value pairs (one per line) */
    char *p = buf;
    int count = 0;

    while (*p && count < CONFIG_MAX_ENTRIES) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        /* Skip comments */
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }

        /* Find key */
        char *key_start = p;
        while (*p && *p != '=' && *p != '\n' && *p != '\r') p++;

        if (*p != '=') {
            /* Invalid line, skip */
            while (*p && *p != '\n') p++;
            continue;
        }

        /* Null-terminate key */
        char *key_end = p;
        *key_end = '\0';
        p++;  /* Skip '=' */

        /* Find value */
        char *val_start = p;
        while (*p && *p != '\n' && *p != '\r') p++;

        char *val_end = p;
        if (*p) {
            *p = '\0';
            p++;
        }

        /* Trim key whitespace */
        while (key_end > key_start && (*(key_end-1) == ' ' || *(key_end-1) == '\t')) {
            key_end--;
            *key_end = '\0';
        }

        /* Trim value whitespace */
        while (*val_start == ' ' || *val_start == '\t') val_start++;
        while (val_end > val_start && (*(val_end-1) == ' ' || *(val_end-1) == '\t')) {
            val_end--;
            *val_end = '\0';
        }

        /* Store entry */
        if (strlen(key_start) > 0 && strlen(key_start) <= CONFIG_KEY_MAX_LEN &&
            strlen(val_start) <= CONFIG_VALUE_MAX_LEN) {
            strncpy(g_config[count].key, key_start, CONFIG_KEY_MAX_LEN);
            strncpy(g_config[count].value, val_start, CONFIG_VALUE_MAX_LEN);
            uart_printf("Config: %s = %s\n", g_config[count].key, g_config[count].value);
            count++;
        }
    }

    g_config_count = count;
    uart_printf("Config: Loaded %d entries\n", count);
    return count;
}

int config_save(void)
{
    uart_puts("Config: Saving system.cfg...\n");

    /* Build config file content */
    char buf[512];
    int pos = 0;

    for (int i = 0; i < g_config_count && pos < (int)sizeof(buf) - 100; i++) {
        int len = strlen(g_config[i].key);
        memcpy(buf + pos, g_config[i].key, len);
        pos += len;

        buf[pos++] = '=';

        len = strlen(g_config[i].value);
        memcpy(buf + pos, g_config[i].value, len);
        pos += len;

        buf[pos++] = '\n';
    }

    /* Delete existing file */
    ekkfs_delete(CONFIG_FILE_NAME, 0);

    /* Create new file */
    int inode = ekkfs_create(CONFIG_FILE_NAME, 0, EKKFS_FLAG_SYSTEM);
    if (inode < 0) {
        uart_printf("Config: Failed to create config file: %d\n", inode);
        return inode;
    }

    /* Write content */
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, CONFIG_FILE_NAME);
    if (ret != EKKFS_OK) {
        return ret;
    }

    int written = ekkfs_write(&file, buf, pos, 0);
    ekkfs_close(&file);

    if (written != pos) {
        uart_printf("Config: Write failed (wrote %d of %d)\n", written, pos);
        return EKKFS_ERR_IO;
    }

    uart_printf("Config: Saved %d entries\n", g_config_count);
    return 0;
}

const char* config_get_str(const char *key, const char *default_val)
{
    int idx = config_find(key);
    if (idx >= 0) {
        return g_config[idx].value;
    }
    return default_val;
}

int config_get_int(const char *key, int default_val)
{
    int idx = config_find(key);
    if (idx >= 0) {
        return simple_atoi(g_config[idx].value);
    }
    return default_val;
}

int config_set(const char *key, const char *value)
{
    if (!key || strlen(key) > CONFIG_KEY_MAX_LEN ||
        !value || strlen(value) > CONFIG_VALUE_MAX_LEN) {
        return -1;
    }

    /* Check if key exists */
    int idx = config_find(key);
    if (idx >= 0) {
        strncpy(g_config[idx].value, value, CONFIG_VALUE_MAX_LEN);
        return 0;
    }

    /* Add new entry */
    if (g_config_count >= CONFIG_MAX_ENTRIES) {
        return -1;  /* Full */
    }

    strncpy(g_config[g_config_count].key, key, CONFIG_KEY_MAX_LEN);
    strncpy(g_config[g_config_count].value, value, CONFIG_VALUE_MAX_LEN);
    g_config_count++;
    return 0;
}

int config_set_int(const char *key, int value)
{
    char buf[16];
    simple_itoa(value, buf, sizeof(buf));
    return config_set(key, buf);
}

int config_count(void)
{
    return g_config_count;
}

/* ============================================================================
 * Logging System with Rotation
 * ============================================================================ */

static struct {
    char filename[16];              /* Current log filename */
    uint32_t max_size;              /* Max size before rotation */
    int max_rotations;              /* Max number of rotation files */
    log_level_t min_level;          /* Minimum log level */
    int initialized;                /* Init flag */
    ekkfs_file_t file;              /* Current log file handle */
    int file_open;                  /* File open flag */
    uint32_t current_size;          /* Current file size */
} g_syslog;

/* Level names for log prefix */
static const char *log_level_names[] = {
    "DEBUG",
    "INFO ",
    "WARN ",
    "ERROR"
};

/* Forward declaration of extern snprintf (from libc_stubs) */
int snprintf(char *buf, size_t size, const char *fmt, ...);

/**
 * @brief Build rotated filename (e.g., "system.log.1")
 */
static void build_rotated_name(char *buf, int bufsize, const char *base, int rotation)
{
    if (rotation == 0) {
        strncpy(buf, base, bufsize - 1);
        buf[bufsize - 1] = '\0';
    } else {
        /* Find position for .N suffix - need to keep under 15 chars */
        int baselen = strlen(base);
        if (baselen > 12) baselen = 12;  /* Leave room for .N */

        int i;
        for (i = 0; i < baselen && i < bufsize - 3; i++) {
            buf[i] = base[i];
        }
        buf[i++] = '.';
        buf[i++] = '0' + (rotation % 10);
        buf[i] = '\0';
    }
}

int syslog_init(const char *filename, uint32_t max_size, int max_rotations)
{
    /* Close existing if open */
    if (g_syslog.file_open) {
        syslog_close();
    }

    /* Set defaults */
    memset(&g_syslog, 0, sizeof(g_syslog));

    if (filename && strlen(filename) > 0) {
        strncpy(g_syslog.filename, filename, 15);
    } else {
        strncpy(g_syslog.filename, LOG_DEFAULT_FILE, 15);
    }
    g_syslog.filename[15] = '\0';

    g_syslog.max_size = (max_size > 0) ? max_size : LOG_MAX_SIZE;
    g_syslog.max_rotations = (max_rotations > 0) ? max_rotations : LOG_MAX_ROTATIONS;
    g_syslog.min_level = LOG_LEVEL_INFO;

    /* Create log file if it doesn't exist */
    ekkfs_stat_t stat;
    if (ekkfs_stat(g_syslog.filename, &stat) != EKKFS_OK) {
        /* Create new file */
        int inode = ekkfs_create(g_syslog.filename, 0, EKKFS_FLAG_LOG);
        if (inode < 0) {
            uart_printf("Syslog: Failed to create %s: %d\n", g_syslog.filename, inode);
            return inode;
        }
        g_syslog.current_size = 0;
    } else {
        g_syslog.current_size = stat.size;
    }

    /* Open log file */
    int ret = ekkfs_open(&g_syslog.file, g_syslog.filename);
    if (ret != EKKFS_OK) {
        uart_printf("Syslog: Failed to open %s: %d\n", g_syslog.filename, ret);
        return ret;
    }

    /* Seek to end for appending */
    ekkfs_seek(&g_syslog.file, g_syslog.current_size);
    g_syslog.file_open = 1;
    g_syslog.initialized = 1;

    uart_printf("Syslog: Initialized %s (size=%lu, max=%lu, rotations=%d)\n",
                g_syslog.filename, g_syslog.current_size,
                g_syslog.max_size, g_syslog.max_rotations);

    return 0;
}

int syslog_rotate(void)
{
    if (!g_syslog.initialized) {
        return -1;
    }

    uart_printf("Syslog: Rotating logs...\n");

    /* Close current file */
    if (g_syslog.file_open) {
        ekkfs_close(&g_syslog.file);
        g_syslog.file_open = 0;
    }

    /* Delete oldest rotation */
    char oldname[16];
    build_rotated_name(oldname, sizeof(oldname), g_syslog.filename, g_syslog.max_rotations);
    ekkfs_delete(oldname, 0);

    /* Rename existing rotations: .2 -> .3, .1 -> .2, etc */
    for (int i = g_syslog.max_rotations - 1; i >= 1; i--) {
        char from[16], to[16];
        build_rotated_name(from, sizeof(from), g_syslog.filename, i);
        build_rotated_name(to, sizeof(to), g_syslog.filename, i + 1);

        /* Check if source exists */
        ekkfs_stat_t stat;
        if (ekkfs_stat(from, &stat) == EKKFS_OK) {
            /* Read content */
            ekkfs_file_t f;
            uint8_t buf[512];

            if (ekkfs_open(&f, from) == EKKFS_OK) {
                /* Create destination */
                ekkfs_delete(to, 0);
                int inode = ekkfs_create(to, 0, EKKFS_FLAG_LOG);
                if (inode >= 0) {
                    ekkfs_file_t fd;
                    if (ekkfs_open(&fd, to) == EKKFS_OK) {
                        /* Copy content */
                        int n;
                        while ((n = ekkfs_read(&f, buf, sizeof(buf))) > 0) {
                            ekkfs_write(&fd, buf, n, 0);
                        }
                        ekkfs_close(&fd);
                    }
                }
                ekkfs_close(&f);
            }
            ekkfs_delete(from, 0);
        }
    }

    /* Rename current to .1 */
    char newname[16];
    build_rotated_name(newname, sizeof(newname), g_syslog.filename, 1);

    ekkfs_stat_t stat;
    if (ekkfs_stat(g_syslog.filename, &stat) == EKKFS_OK && stat.size > 0) {
        /* Read current log */
        ekkfs_file_t f;
        if (ekkfs_open(&f, g_syslog.filename) == EKKFS_OK) {
            /* Create .1 file */
            ekkfs_delete(newname, 0);
            int inode = ekkfs_create(newname, 0, EKKFS_FLAG_LOG);
            if (inode >= 0) {
                ekkfs_file_t fd;
                if (ekkfs_open(&fd, newname) == EKKFS_OK) {
                    uint8_t buf[512];
                    int n;
                    while ((n = ekkfs_read(&f, buf, sizeof(buf))) > 0) {
                        ekkfs_write(&fd, buf, n, 0);
                    }
                    ekkfs_close(&fd);
                }
            }
            ekkfs_close(&f);
        }
    }

    /* Delete and recreate main log */
    ekkfs_delete(g_syslog.filename, 0);
    int inode = ekkfs_create(g_syslog.filename, 0, EKKFS_FLAG_LOG);
    if (inode < 0) {
        uart_printf("Syslog: Failed to recreate log: %d\n", inode);
        return inode;
    }

    /* Reopen file */
    int ret = ekkfs_open(&g_syslog.file, g_syslog.filename);
    if (ret != EKKFS_OK) {
        uart_printf("Syslog: Failed to reopen log: %d\n", ret);
        return ret;
    }

    g_syslog.file_open = 1;
    g_syslog.current_size = 0;

    uart_puts("Syslog: Rotation complete\n");
    return 0;
}

int syslog_write(log_level_t level, const char *format, ...)
{
    if (!g_syslog.initialized || !g_syslog.file_open) {
        return -1;
    }

    /* Check level */
    if (level < g_syslog.min_level) {
        return 0;  /* Filtered out */
    }

    /* Check if rotation needed */
    if (g_syslog.current_size >= g_syslog.max_size) {
        syslog_rotate();
    }

    /* Build log line */
    char line[LOG_BUFFER_SIZE];
    int pos = 0;

    /* Add level prefix */
    const char *lvl = (level <= LOG_LEVEL_ERROR) ? log_level_names[level] : "?????";
    pos = snprintf(line, sizeof(line), "[%s] ", lvl);

    /* Add formatted message */
    __builtin_va_list args;
    __builtin_va_start(args, format);

    /* Simple vsnprintf-like formatting */
    const char *f = format;
    while (*f && pos < (int)sizeof(line) - 2) {
        if (*f != '%') {
            line[pos++] = *f++;
            continue;
        }
        f++;

        switch (*f) {
            case 'd':
            case 'i': {
                int val = __builtin_va_arg(args, int);
                char tmp[16];
                simple_itoa(val, tmp, sizeof(tmp));
                int len = strlen(tmp);
                if (pos + len < (int)sizeof(line) - 1) {
                    memcpy(line + pos, tmp, len);
                    pos += len;
                }
                break;
            }
            case 's': {
                const char *s = __builtin_va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && pos < (int)sizeof(line) - 2) {
                    line[pos++] = *s++;
                }
                break;
            }
            case '%':
                line[pos++] = '%';
                break;
            default:
                line[pos++] = *f;
                break;
        }
        f++;
    }

    __builtin_va_end(args);

    /* Add newline */
    line[pos++] = '\n';
    line[pos] = '\0';

    /* Write to file */
    int written = ekkfs_write(&g_syslog.file, line, pos, 0);
    if (written > 0) {
        g_syslog.current_size += written;
    }

    return written;
}

void syslog_close(void)
{
    if (g_syslog.file_open) {
        ekkfs_close(&g_syslog.file);
        g_syslog.file_open = 0;
    }
    g_syslog.initialized = 0;
    uart_puts("Syslog: Closed\n");
}

void syslog_set_level(log_level_t level)
{
    g_syslog.min_level = level;
}

uint32_t syslog_get_size(void)
{
    return g_syslog.current_size;
}
