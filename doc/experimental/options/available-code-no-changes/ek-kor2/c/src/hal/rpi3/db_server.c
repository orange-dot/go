/**
 * @file db_server.c
 * @brief EKKDB Server Module Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * This module implements the database server for the EK-KOR2 microkernel.
 * It runs on Core 3 alongside the FS server and handles all database
 * operations via IPC message passing.
 */

#include "db_server.h"
#include "../../ekkdb.h"
#include "uart.h"
#include <string.h>

/* ============================================================================
 * State
 * ============================================================================ */

/* Server ready flag */
static volatile int g_db_server_ready = 0;

/* ============================================================================
 * Request Handlers - Key-Value
 * ============================================================================ */

static void handle_kv_open(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    const char *namespace_name = (const char *)req->data;

    int result = ekkdb_kv_server_open(namespace_name, req->sender_id);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;  /* Handle */
        uart_printf("DB: KV opened '%s' (handle %d)\n", namespace_name, result);
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
        uart_printf("DB: KV open '%s' failed: %d\n", namespace_name, result);
    }
}

static void handle_kv_close(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_kv_server_close(req->handle);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_kv_get(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    const char *key = (const char *)req->data;
    uint32_t len = req->param1;

    if (len > sizeof(resp->data)) {
        len = sizeof(resp->data);
    }

    int result = ekkdb_kv_server_get(req->handle, key, resp->data, &len);
    if (result == EKKDB_OK) {
        resp->status = 0;
        resp->result = len;  /* Actual value length */
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_kv_put(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    /* Parse key and value from data field */
    const char *key = (const char *)req->data;
    size_t key_len = strlen(key);
    const void *value = req->data + key_len + 1;
    uint32_t value_len = req->param1;

    int result = ekkdb_kv_server_put(req->handle, key, value, value_len);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_kv_delete(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    const char *key = (const char *)req->data;

    int result = ekkdb_kv_server_delete(req->handle, key);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_kv_count(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_kv_server_count(req->handle);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

/* ============================================================================
 * Request Handlers - Time-Series
 * ============================================================================ */

static void handle_ts_open(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    uint8_t module_id = (uint8_t)req->param1;
    const char *metric = (const char *)req->data;

    int result = ekkdb_ts_server_open(module_id, metric, req->sender_id);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;  /* Handle */
        uart_printf("DB: TS opened mod%d/%s (handle %d)\n", module_id, metric, result);
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_ts_close(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_ts_server_close(req->handle);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_ts_append(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    ekkdb_ts_record_t record;
    memcpy(&record, req->data, sizeof(record));

    int result = ekkdb_ts_server_append(req->handle, &record);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_ts_query(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    uint64_t start_us, end_us;
    memcpy(&start_us, req->data, sizeof(start_us));
    memcpy(&end_us, req->data + 8, sizeof(end_us));

    uint32_t iter_handle, total_count;
    int result = ekkdb_ts_server_query(req->handle, start_us, end_us,
                                       &iter_handle, &total_count);
    if (result == EKKDB_OK) {
        resp->status = 0;
        resp->result = iter_handle;
        memcpy(resp->data, &total_count, sizeof(total_count));
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_ts_next(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    ekkdb_ts_record_t record;
    int result = ekkdb_ts_server_next(req->handle, &record);
    if (result == EKKDB_OK) {
        resp->status = 0;
        resp->result = 0;
        memcpy(resp->data, &record, sizeof(record));
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_ts_iter_close(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_ts_server_iter_close(req->handle);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_ts_compact(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_ts_server_compact(req->handle);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_ts_count(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_ts_server_count(req->handle);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

/* ============================================================================
 * Request Handlers - Event Log
 * ============================================================================ */

static void handle_log_open(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_log_server_open(req->sender_id);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;  /* Handle (always 0 for log) */
        uart_printf("DB: Log opened (handle %d)\n", result);
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_log_close(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_log_server_close(req->handle);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_log_write(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    /* Reconstruct event from request */
    ekkdb_event_t event;
    memcpy(&event, req->data, 48);
    memcpy(((uint8_t*)&event) + 48, &req->param1, 8);
    memcpy(((uint8_t*)&event) + 56, &req->param2, 8);

    int result = ekkdb_log_server_write(req->handle, &event);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_log_query(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    ekkdb_log_filter_t filter;
    memcpy(&filter, req->data, sizeof(filter));

    /* Check if filter is zeroed (means no filter) */
    ekkdb_log_filter_t zero_filter;
    memset(&zero_filter, 0, sizeof(zero_filter));
    ekkdb_log_filter_t *filter_ptr = (memcmp(&filter, &zero_filter, sizeof(filter)) == 0) ? NULL : &filter;

    uint32_t iter_handle, total_count;
    int result = ekkdb_log_server_query(req->handle, filter_ptr,
                                        &iter_handle, &total_count);
    if (result == EKKDB_OK) {
        resp->status = 0;
        resp->result = iter_handle;
        memcpy(resp->data, &total_count, sizeof(total_count));
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_log_next(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    ekkdb_event_t event;
    int result = ekkdb_log_server_next(req->handle, &event);
    if (result == EKKDB_OK) {
        resp->status = 0;
        /* Pack event into response (56 bytes in data, 4 in result) */
        memcpy(resp->data, &event, 56);
        memcpy(&resp->result, ((uint8_t*)&event) + 56, 4);
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

static void handle_log_iter_close(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_log_server_iter_close(req->handle);
    resp->status = (result == EKKDB_OK) ? 0 : (uint8_t)(-result);
    resp->result = 0;
}

static void handle_log_count(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    int result = ekkdb_log_server_count(req->handle);
    if (result >= 0) {
        resp->status = 0;
        resp->result = result;
    } else {
        resp->status = (uint8_t)(-result);
        resp->result = 0;
    }
}

/* ============================================================================
 * DB Server Main Functions
 * ============================================================================ */

void db_server_handle_request(const ekkdb_request_t *req, ekkdb_response_t *resp)
{
    /* Initialize response */
    memset(resp, 0, sizeof(*resp));
    resp->cmd = req->cmd;
    resp->req_id = req->req_id;

    switch (req->cmd) {
        /* Key-Value commands */
        case EKKDB_IPC_KV_OPEN:
            handle_kv_open(req, resp);
            break;
        case EKKDB_IPC_KV_CLOSE:
            handle_kv_close(req, resp);
            break;
        case EKKDB_IPC_KV_GET:
            handle_kv_get(req, resp);
            break;
        case EKKDB_IPC_KV_PUT:
            handle_kv_put(req, resp);
            break;
        case EKKDB_IPC_KV_DELETE:
            handle_kv_delete(req, resp);
            break;
        case EKKDB_IPC_KV_COUNT:
            handle_kv_count(req, resp);
            break;

        /* Time-Series commands */
        case EKKDB_IPC_TS_OPEN:
            handle_ts_open(req, resp);
            break;
        case EKKDB_IPC_TS_CLOSE:
            handle_ts_close(req, resp);
            break;
        case EKKDB_IPC_TS_APPEND:
            handle_ts_append(req, resp);
            break;
        case EKKDB_IPC_TS_QUERY:
            handle_ts_query(req, resp);
            break;
        case EKKDB_IPC_TS_NEXT:
            handle_ts_next(req, resp);
            break;
        case EKKDB_IPC_TS_ITER_CLOSE:
            handle_ts_iter_close(req, resp);
            break;
        case EKKDB_IPC_TS_COMPACT:
            handle_ts_compact(req, resp);
            break;
        case EKKDB_IPC_TS_COUNT:
            handle_ts_count(req, resp);
            break;

        /* Event Log commands */
        case EKKDB_IPC_LOG_OPEN:
            handle_log_open(req, resp);
            break;
        case EKKDB_IPC_LOG_CLOSE:
            handle_log_close(req, resp);
            break;
        case EKKDB_IPC_LOG_WRITE:
            handle_log_write(req, resp);
            break;
        case EKKDB_IPC_LOG_QUERY:
            handle_log_query(req, resp);
            break;
        case EKKDB_IPC_LOG_NEXT:
            handle_log_next(req, resp);
            break;
        case EKKDB_IPC_LOG_ITER_CLOSE:
            handle_log_iter_close(req, resp);
            break;
        case EKKDB_IPC_LOG_COUNT:
            handle_log_count(req, resp);
            break;

        default:
            resp->status = (uint8_t)(-EKKDB_ERR_INVALID);
            uart_printf("DB: Unknown command %d\n", req->cmd);
            break;
    }
}

int db_server_init(void)
{
    uart_puts("DB Server: Initializing...\n");

    /* Initialize database subsystem */
    int result = ekkdb_init();
    if (result != 0) {
        uart_printf("DB Server: Init failed: %d\n", result);
        return result;
    }

    /* Mark server as ready */
    __asm__ volatile("dmb sy" ::: "memory");
    g_db_server_ready = 1;
    __asm__ volatile("dmb sy" ::: "memory");

    uart_puts("DB Server: Ready\n");
    return 0;
}

int db_server_is_ready(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
    return g_db_server_ready;
}

/* ============================================================================
 * Database Initialization
 * ============================================================================ */

int ekkdb_init(void)
{
    /* No additional initialization needed - state is statically initialized */
    /* The KV, TS, and Log modules initialize on first open */
    return 0;
}
