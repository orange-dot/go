/**
 * @file ekkdb_client.c
 * @brief EKKDB Client Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements the client-side API for accessing EKKDB via IPC.
 * Communicates with the DB server running on Core 3 (same as FS server).
 */

#include "ekk/ekk_db.h"
#include "ekk/ekk_hal.h"
#include "ekkdb.h"

#ifdef EKK_PLATFORM_RPI3
#include "hal/rpi3/fs_server.h"
#include "hal/rpi3/db_server.h"
#include "hal/rpi3/msg_queue.h"
#include "hal/rpi3/smp.h"
#endif

#include <string.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define DB_REQUEST_TIMEOUT_US   1000000     /* 1 second timeout */

/* ============================================================================
 * Static State
 * ============================================================================ */

static uint16_t g_db_request_id = 1;

/* ============================================================================
 * IPC Helper Functions
 * ============================================================================ */

#ifdef EKK_PLATFORM_RPI3

/**
 * @brief Send DB request and wait for response
 */
static int db_send_request(ekkdb_request_t *req, ekkdb_response_t *resp)
{
    /* Assign request ID */
    req->req_id = g_db_request_id++;
    req->sender_id = ekk_hal_get_module_id();

    /* Send request to DB server (Core 3) */
    uint32_t fs_core = fs_server_get_core();
    if (msg_queue_send(fs_core, req->sender_id, MSG_TYPE_DB_REQUEST,
                       req, sizeof(ekkdb_request_t)) < 0) {
        return EKKDB_ERR_IO;
    }

    /* Wait for response */
    uint64_t start = ekk_hal_time_us();
    uint8_t sender_id, msg_type;
    uint32_t len;

    while ((ekk_hal_time_us() - start) < DB_REQUEST_TIMEOUT_US) {
        len = sizeof(ekkdb_response_t);
        if (msg_queue_recv(&sender_id, &msg_type, resp, &len) == 0) {
            if (msg_type == MSG_TYPE_DB_RESPONSE && resp->req_id == req->req_id) {
                /* Got matching response */
                if (resp->status != 0) {
                    return -((int)resp->status);
                }
                return EKKDB_OK;
            }
        }
        /* Brief yield */
        __asm__ volatile("yield");
    }

    return EKKDB_ERR_TIMEOUT;
}

#else
/* Non-RPi3 stub */
static int db_send_request(ekkdb_request_t *req, ekkdb_response_t *resp)
{
    (void)req;
    (void)resp;
    return EKKDB_ERR_NOT_READY;
}
#endif

/* ============================================================================
 * Server Ready Check
 * ============================================================================ */

int ekkdb_is_ready(void)
{
#ifdef EKK_PLATFORM_RPI3
    return db_server_is_ready();
#else
    return 0;
#endif
}

/* ============================================================================
 * Key-Value Client API
 * ============================================================================ */

int ekkdb_kv_open(const char *namespace_name, ekkdb_kv_t *kv)
{
    if (!ekkdb_is_ready()) {
        return EKKDB_ERR_NOT_READY;
    }
    if (!namespace_name || !kv) {
        return EKKDB_ERR_INVALID;
    }
    if (strlen(namespace_name) > EKKDB_KV_MAX_NAMESPACE) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_KV_OPEN;
    strncpy((char *)req.data, namespace_name, sizeof(req.data) - 1);

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    kv->handle = resp.result;
    kv->owner_id = ekk_hal_get_module_id();
    strncpy(kv->namespace_name, namespace_name, EKKDB_KV_MAX_NAMESPACE);
    kv->is_open = 1;

    return EKKDB_OK;
#else
    (void)namespace_name;
    (void)kv;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_kv_get(ekkdb_kv_t *kv, const char *key, void *value, uint32_t *len)
{
    if (!kv || !kv->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!key || !value || !len || *len == 0) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_KV_GET;
    req.handle = kv->handle;
    req.param1 = *len;
    strncpy((char *)req.data, key, sizeof(req.data) - 1);

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    /* Copy value from response */
    uint32_t copy_len = resp.result;
    if (copy_len > *len) {
        copy_len = *len;
    }
    memcpy(value, resp.data, copy_len);
    *len = resp.result;

    return EKKDB_OK;
#else
    (void)kv;
    (void)key;
    (void)value;
    (void)len;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_kv_put(ekkdb_kv_t *kv, const char *key, const void *value, uint32_t len)
{
    if (!kv || !kv->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!key) {
        return EKKDB_ERR_INVALID;
    }
    if (len > EKKDB_KV_MAX_VALUE_LEN) {
        return EKKDB_ERR_VALUE_TOO_BIG;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_KV_PUT;
    req.handle = kv->handle;
    req.param1 = len;

    /* Pack key and value into data field */
    /* Format: key\0value */
    size_t key_len = strlen(key);
    if (key_len > 15) key_len = 15;

    memcpy(req.data, key, key_len);
    req.data[key_len] = '\0';

    if (value && len > 0) {
        memcpy(req.data + key_len + 1, value, len);
    }

    return db_send_request(&req, &resp);
#else
    (void)kv;
    (void)key;
    (void)value;
    (void)len;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_kv_delete(ekkdb_kv_t *kv, const char *key)
{
    if (!kv || !kv->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!key) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_KV_DELETE;
    req.handle = kv->handle;
    strncpy((char *)req.data, key, sizeof(req.data) - 1);

    return db_send_request(&req, &resp);
#else
    (void)kv;
    (void)key;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_kv_close(ekkdb_kv_t *kv)
{
    if (!kv || !kv->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_KV_CLOSE;
    req.handle = kv->handle;

    int result = db_send_request(&req, &resp);

    kv->is_open = 0;
    kv->handle = 0;

    return result;
#else
    (void)kv;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_kv_count(ekkdb_kv_t *kv)
{
    if (!kv || !kv->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_KV_COUNT;
    req.handle = kv->handle;

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    return (int)resp.result;
#else
    (void)kv;
    return EKKDB_ERR_NOT_READY;
#endif
}

/* ============================================================================
 * Time-Series Client API
 * ============================================================================ */

int ekkdb_ts_open(uint8_t module_id, const char *metric, ekkdb_ts_t *ts)
{
    if (!ekkdb_is_ready()) {
        return EKKDB_ERR_NOT_READY;
    }
    if (!metric || !ts) {
        return EKKDB_ERR_INVALID;
    }
    if (strlen(metric) > EKKDB_TS_MAX_METRIC_LEN) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_OPEN;
    req.param1 = module_id;
    strncpy((char *)req.data, metric, sizeof(req.data) - 1);

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    ts->handle = resp.result;
    ts->module_id = module_id;
    strncpy(ts->metric, metric, EKKDB_TS_MAX_METRIC_LEN);
    ts->is_open = 1;

    return EKKDB_OK;
#else
    (void)module_id;
    (void)metric;
    (void)ts;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_ts_append(ekkdb_ts_t *ts, const ekkdb_ts_record_t *record)
{
    if (!ts || !ts->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!record) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_APPEND;
    req.handle = ts->handle;
    memcpy(req.data, record, sizeof(ekkdb_ts_record_t));

    return db_send_request(&req, &resp);
#else
    (void)ts;
    (void)record;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_ts_query(ekkdb_ts_t *ts, uint64_t start_us, uint64_t end_us, ekkdb_ts_iter_t *iter)
{
    if (!ts || !ts->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!iter) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_QUERY;
    req.handle = ts->handle;

    /* Pack time range into data */
    memcpy(req.data, &start_us, sizeof(start_us));
    memcpy(req.data + 8, &end_us, sizeof(end_us));

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    iter->handle = resp.result;
    iter->start_us = start_us;
    iter->end_us = end_us;
    iter->current_idx = 0;

    /* Unpack total count from response */
    memcpy(&iter->total_count, resp.data, sizeof(iter->total_count));
    iter->is_valid = 1;

    return EKKDB_OK;
#else
    (void)ts;
    (void)start_us;
    (void)end_us;
    (void)iter;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_ts_next(ekkdb_ts_iter_t *iter, ekkdb_ts_record_t *record)
{
    if (!iter || !iter->is_valid) {
        return EKKDB_ERR_INVALID;
    }
    if (!record) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_NEXT;
    req.handle = iter->handle;

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    /* Unpack record from response */
    memcpy(record, resp.data, sizeof(ekkdb_ts_record_t));
    iter->current_idx++;

    return EKKDB_OK;
#else
    (void)iter;
    (void)record;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_ts_iter_close(ekkdb_ts_iter_t *iter)
{
    if (!iter || !iter->is_valid) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_ITER_CLOSE;
    req.handle = iter->handle;

    int result = db_send_request(&req, &resp);

    iter->is_valid = 0;
    iter->handle = 0;

    return result;
#else
    (void)iter;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_ts_compact(ekkdb_ts_t *ts)
{
    if (!ts || !ts->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_COMPACT;
    req.handle = ts->handle;

    return db_send_request(&req, &resp);
#else
    (void)ts;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_ts_close(ekkdb_ts_t *ts)
{
    if (!ts || !ts->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_CLOSE;
    req.handle = ts->handle;

    int result = db_send_request(&req, &resp);

    ts->is_open = 0;
    ts->handle = 0;

    return result;
#else
    (void)ts;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_ts_count(ekkdb_ts_t *ts)
{
    if (!ts || !ts->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_TS_COUNT;
    req.handle = ts->handle;

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    return (int)resp.result;
#else
    (void)ts;
    return EKKDB_ERR_NOT_READY;
#endif
}

/* ============================================================================
 * Event Log Client API
 * ============================================================================ */

int ekkdb_log_open(ekkdb_log_t *log)
{
    if (!ekkdb_is_ready()) {
        return EKKDB_ERR_NOT_READY;
    }
    if (!log) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_LOG_OPEN;

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    log->handle = resp.result;
    log->is_open = 1;

    return EKKDB_OK;
#else
    (void)log;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_log_write(ekkdb_log_t *log, const ekkdb_event_t *event)
{
    if (!log || !log->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!event) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_LOG_WRITE;
    req.handle = log->handle;

    /* Pack event into data (48 bytes available, event is 64 bytes) */
    /* We'll need to split across param fields and data */
    memcpy(req.data, event, 48);
    /* Remaining 16 bytes go in param1/param2 (cast as pointers to event+48) */
    memcpy(&req.param1, ((uint8_t*)event) + 48, 8);
    memcpy(&req.param2, ((uint8_t*)event) + 56, 8);

    return db_send_request(&req, &resp);
#else
    (void)log;
    (void)event;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_log_query(ekkdb_log_t *log, const ekkdb_log_filter_t *filter, ekkdb_log_iter_t *iter)
{
    if (!log || !log->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!iter) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_LOG_QUERY;
    req.handle = log->handle;

    /* Pack filter into data */
    if (filter) {
        memcpy(req.data, filter, sizeof(ekkdb_log_filter_t));
    }

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    iter->handle = resp.result;
    if (filter) {
        memcpy(&iter->filter, filter, sizeof(iter->filter));
    } else {
        memset(&iter->filter, 0, sizeof(iter->filter));
    }
    iter->current_idx = 0;

    /* Unpack total count from response */
    memcpy(&iter->total_count, resp.data, sizeof(iter->total_count));
    iter->is_valid = 1;

    return EKKDB_OK;
#else
    (void)log;
    (void)filter;
    (void)iter;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_log_next(ekkdb_log_iter_t *iter, ekkdb_event_t *event)
{
    if (!iter || !iter->is_valid) {
        return EKKDB_ERR_INVALID;
    }
    if (!event) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_LOG_NEXT;
    req.handle = iter->handle;

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    /* Unpack event from response (56 bytes in data, need to reconstruct 64) */
    memcpy(event, resp.data, 56);
    /* Get remaining bytes from result field (repurposed) */
    memcpy(((uint8_t*)event) + 56, &resp.result, 4);
    /* Last 4 bytes are in the CRC position - we recalculate anyway */

    iter->current_idx++;

    return EKKDB_OK;
#else
    (void)iter;
    (void)event;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_log_iter_close(ekkdb_log_iter_t *iter)
{
    if (!iter || !iter->is_valid) {
        return EKKDB_ERR_INVALID;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_LOG_ITER_CLOSE;
    req.handle = iter->handle;

    int result = db_send_request(&req, &resp);

    iter->is_valid = 0;
    iter->handle = 0;

    return result;
#else
    (void)iter;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_log_close(ekkdb_log_t *log)
{
    if (!log || !log->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_LOG_CLOSE;
    req.handle = log->handle;

    int result = db_send_request(&req, &resp);

    log->is_open = 0;
    log->handle = 0;

    return result;
#else
    (void)log;
    return EKKDB_ERR_NOT_READY;
#endif
}

int ekkdb_log_count(ekkdb_log_t *log)
{
    if (!log || !log->is_open) {
        return EKKDB_ERR_NOT_OPEN;
    }

#ifdef EKK_PLATFORM_RPI3
    ekkdb_request_t req;
    ekkdb_response_t resp;

    memset(&req, 0, sizeof(req));
    req.cmd = EKKDB_IPC_LOG_COUNT;
    req.handle = log->handle;

    int result = db_send_request(&req, &resp);
    if (result != EKKDB_OK) {
        return result;
    }

    return (int)resp.result;
#else
    (void)log;
    return EKKDB_ERR_NOT_READY;
#endif
}
