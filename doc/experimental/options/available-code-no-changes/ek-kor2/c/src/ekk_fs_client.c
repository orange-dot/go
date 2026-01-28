/**
 * @file ekk_fs_client.c
 * @brief EK-KOR Filesystem Client Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements the client-side API for accessing EKKFS via IPC.
 * Communicates with the FS server running on Core 3.
 */

#include "ekk/ekk_fs.h"
#include "ekk/ekk_hal.h"

#ifdef EKK_PLATFORM_RPI3
#include "hal/rpi3/fs_server.h"
#include "hal/rpi3/msg_queue.h"
#include "hal/rpi3/smp.h"
#endif

#include <string.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define FS_REQUEST_TIMEOUT_US   1000000     /* 1 second timeout */
#define FS_MAX_INLINE_DATA      48          /* Max data in single IPC message */

/* ============================================================================
 * Static State
 * ============================================================================ */

static uint16_t g_request_id = 1;

/* ============================================================================
 * IPC Helper Functions
 * ============================================================================ */

#ifdef EKK_PLATFORM_RPI3

/**
 * @brief Send FS request and wait for response
 */
static int fs_send_request(ekkfs_request_t *req, ekkfs_response_t *resp)
{
    /* Assign request ID */
    req->req_id = g_request_id++;
    req->sender_id = ekk_hal_get_module_id();

    /* Send request to FS server (Core 3) */
    uint32_t fs_core = fs_server_get_core();
    if (msg_queue_send(fs_core, req->sender_id, MSG_TYPE_FS_REQUEST,
                       req, sizeof(ekkfs_request_t)) < 0) {
        return EKK_FS_ERR_IO;
    }

    /* Wait for response */
    uint64_t start = ekk_hal_time_us();
    uint8_t sender_id, msg_type;
    uint32_t len;

    while ((ekk_hal_time_us() - start) < FS_REQUEST_TIMEOUT_US) {
        len = sizeof(ekkfs_response_t);
        if (msg_queue_recv(&sender_id, &msg_type, resp, &len) == 0) {
            if (msg_type == MSG_TYPE_FS_RESPONSE && resp->req_id == req->req_id) {
                /* Got matching response */
                if (resp->status != 0) {
                    return -((int)resp->status);
                }
                return EKK_FS_OK;
            }
        }
        /* Brief yield */
        __asm__ volatile("yield");
    }

    return EKK_FS_ERR_TIMEOUT;
}

#else
/* Non-RPi3 stub implementations */
static int fs_send_request(void *req, void *resp)
{
    (void)req;
    (void)resp;
    return EKK_FS_ERR_NOT_READY;
}
#endif

/* ============================================================================
 * Client API Implementation
 * ============================================================================ */

int ekk_fs_is_ready(void)
{
#ifdef EKK_PLATFORM_RPI3
    return fs_server_is_ready();
#else
    return 0;
#endif
}

int ekk_fs_create(const char *name, uint32_t flags)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

    size_t name_len = strlen(name);
    if (name_len > 15) {
        return EKK_FS_ERR_NAME_TOO_LONG;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_CREATE;
    req.offset = flags;  /* flags passed in offset field */
    strncpy((char *)req.data, name, sizeof(req.data) - 1);

    int result = fs_send_request(&req, &resp);
    if (result != EKK_FS_OK) {
        return result;
    }

    /* Open the file and return handle */
    return ekk_fs_open(name);
#else
    (void)name;
    (void)flags;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_delete(const char *name)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_DELETE;
    strncpy((char *)req.data, name, sizeof(req.data) - 1);

    return fs_send_request(&req, &resp);
#else
    (void)name;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_open(const char *name)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_OPEN;
    strncpy((char *)req.data, name, sizeof(req.data) - 1);

    int result = fs_send_request(&req, &resp);
    if (result != EKK_FS_OK) {
        return result;
    }

    return (int)resp.result;  /* File handle */
#else
    (void)name;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_close(int fd)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_CLOSE;
    req.inode = fd;

    return fs_send_request(&req, &resp);
#else
    (void)fd;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_read(int fd, void *buffer, size_t size)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    /* Read in chunks due to IPC message size limit */
    uint8_t *out = (uint8_t *)buffer;
    size_t total_read = 0;

    while (total_read < size) {
        ekkfs_request_t req;
        ekkfs_response_t resp;

        memset(&req, 0, sizeof(req));
        req.cmd = EKKFS_IPC_READ;
        req.inode = fd;

        size_t chunk = size - total_read;
        if (chunk > sizeof(resp.data)) {
            chunk = sizeof(resp.data);
        }
        req.length = chunk;

        int result = fs_send_request(&req, &resp);
        if (result != EKK_FS_OK) {
            return (total_read > 0) ? (int)total_read : result;
        }

        if (resp.result == 0) {
            break;  /* EOF */
        }

        memcpy(out + total_read, resp.data, resp.result);
        total_read += resp.result;

        if (resp.result < chunk) {
            break;  /* EOF or partial read */
        }
    }

    return (int)total_read;
#else
    (void)fd;
    (void)buffer;
    (void)size;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_write(int fd, const void *buffer, size_t size)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    /* Write in chunks due to IPC message size limit */
    const uint8_t *in = (const uint8_t *)buffer;
    size_t total_written = 0;

    while (total_written < size) {
        ekkfs_request_t req;
        ekkfs_response_t resp;

        memset(&req, 0, sizeof(req));
        req.cmd = EKKFS_IPC_WRITE;
        req.inode = fd;

        size_t chunk = size - total_written;
        if (chunk > FS_MAX_INLINE_DATA) {
            chunk = FS_MAX_INLINE_DATA;
        }
        req.length = chunk;
        memcpy(req.data, in + total_written, chunk);

        int result = fs_send_request(&req, &resp);
        if (result != EKK_FS_OK) {
            return (total_written > 0) ? (int)total_written : result;
        }

        total_written += resp.result;

        if (resp.result < chunk) {
            break;  /* Partial write (disk full?) */
        }
    }

    return (int)total_written;
#else
    (void)fd;
    (void)buffer;
    (void)size;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_seek(int fd, uint32_t position)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_SEEK;
    req.inode = fd;
    req.offset = position;

    int result = fs_send_request(&req, &resp);
    if (result != EKK_FS_OK) {
        return result;
    }

    return (int)resp.result;  /* New position */
#else
    (void)fd;
    (void)position;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_stat(const char *name, ekk_fs_stat_t *stat)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_STAT;
    strncpy((char *)req.data, name, sizeof(req.data) - 1);

    int result = fs_send_request(&req, &resp);
    if (result != EKK_FS_OK) {
        return result;
    }

    /* Copy stat data from response */
    memcpy(stat, resp.data, sizeof(ekk_fs_stat_t));
    return EKK_FS_OK;
#else
    (void)name;
    (void)stat;
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_sync(void)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_SYNC;

    return fs_send_request(&req, &resp);
#else
    return EKK_FS_ERR_NOT_READY;
#endif
}

int ekk_fs_statfs(ekk_fs_statfs_t *statfs)
{
    if (!ekk_fs_is_ready()) {
        return EKK_FS_ERR_NOT_READY;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkfs_request_t req;
    ekkfs_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKFS_IPC_STATFS;

    int result = fs_send_request(&req, &resp);
    if (result != EKK_FS_OK) {
        return result;
    }

    /* Unpack stats from response */
    uint32_t *data32 = (uint32_t *)resp.data;
    statfs->total_blocks = data32[0];
    statfs->free_blocks = data32[1];
    statfs->total_inodes = data32[2];
    statfs->used_inodes = data32[3];

    return EKK_FS_OK;
#else
    (void)statfs;
    return EKK_FS_ERR_NOT_READY;
#endif
}

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

int ekk_fs_read_file(const char *name, void *buffer, size_t max_size)
{
    int fd = ekk_fs_open(name);
    if (fd < 0) {
        return fd;
    }

    int result = ekk_fs_read(fd, buffer, max_size);
    ekk_fs_close(fd);

    return result;
}

int ekk_fs_write_file(const char *name, const void *buffer, size_t size, uint32_t flags)
{
    /* Try to delete existing file first */
    ekk_fs_delete(name);

    /* Create new file */
    int fd = ekk_fs_create(name, flags);
    if (fd < 0) {
        return fd;
    }

    int result = ekk_fs_write(fd, buffer, size);
    ekk_fs_close(fd);

    if (result < 0) {
        return result;
    }
    if ((size_t)result != size) {
        return EKK_FS_ERR_FULL;
    }

    return EKK_FS_OK;
}

int ekk_fs_append(const char *name, const void *buffer, size_t size)
{
    /* Get file size */
    ekk_fs_stat_t stat;
    int result = ekk_fs_stat(name, &stat);
    if (result != EKK_FS_OK) {
        /* File doesn't exist, create it */
        return ekk_fs_write_file(name, buffer, size, 0);
    }

    /* Open and seek to end */
    int fd = ekk_fs_open(name);
    if (fd < 0) {
        return fd;
    }

    ekk_fs_seek(fd, stat.size);

    result = ekk_fs_write(fd, buffer, size);
    ekk_fs_close(fd);

    return result;
}

int ekk_fs_exists(const char *name)
{
    ekk_fs_stat_t stat;
    return (ekk_fs_stat(name, &stat) == EKK_FS_OK) ? 1 : 0;
}
