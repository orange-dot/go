/**
 * @file db_server.h
 * @brief EKKDB Server Module for EK-KOR2 Microkernel
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * The DB server runs alongside the FS server on Core 3.
 * It handles all database operations via IPC message passing.
 */

#ifndef RPI3_DB_SERVER_H
#define RPI3_DB_SERVER_H

#include <stdint.h>
#include "../../ekkdb.h"

/* ============================================================================
 * DB Server Functions
 * ============================================================================ */

/**
 * @brief Initialize the DB server
 *
 * Initializes all database subsystems (KV, TS, Log).
 * Called by fs_server during init, after filesystem is mounted.
 *
 * @return 0 on success, negative error code on failure
 */
int db_server_init(void);

/**
 * @brief Process a single DB request
 *
 * Called by the fs_server main loop for DB IPC messages.
 *
 * @param req Request message
 * @param resp Response message (filled in)
 */
void db_server_handle_request(const ekkdb_request_t *req, ekkdb_response_t *resp);

/**
 * @brief Check if DB server is ready
 *
 * @return 1 if ready to accept requests, 0 if not
 */
int db_server_is_ready(void);

#endif /* RPI3_DB_SERVER_H */
