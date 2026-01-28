/**
 * @file ekkdb_kv.c
 * @brief EKKDB Key-Value Store Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements a simple key-value store with:
 * - Hash table with linear probing
 * - Fixed 32-byte entries (16 per 512-byte block)
 * - Max 14-byte keys and 14-byte inline values
 * - Stored on EKKFS filesystem
 */

#include "ekkdb.h"
#include "ekkfs.h"
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static ekkdb_kv_state_t g_kv_handles[EKKDB_MAX_KV_HANDLES];
static int g_kv_initialized = 0;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief djb2 hash function
 */
uint32_t ekkdb_hash(const char *key)
{
    uint32_t hash = 5381;
    int c;

    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    }

    return hash;
}

/**
 * @brief Calculate CRC32 for header
 */
uint32_t ekkdb_header_crc(const void *header, size_t size)
{
    return ekkfs_crc32(header, size - sizeof(uint32_t));
}

/**
 * @brief Build KV filename from namespace
 */
static void build_kv_filename(char *buf, size_t buflen, const char *namespace_name)
{
    /* Format: kv_<namespace>.dat - max 15 chars total */
    int ns_len = strlen(namespace_name);
    if (ns_len > 8) ns_len = 8;  /* Truncate to fit */

    buf[0] = 'k';
    buf[1] = 'v';
    buf[2] = '_';
    memcpy(buf + 3, namespace_name, ns_len);
    memcpy(buf + 3 + ns_len, ".dat", 5);
}

/**
 * @brief Read KV entry from file
 */
static int read_kv_entry(const char *filename, uint32_t index, ekkdb_kv_entry_t *entry)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    /* Calculate offset: header (64 bytes) + index * entry_size */
    uint32_t offset = sizeof(ekkdb_kv_header_t) + index * EKKDB_KV_ENTRY_SIZE;
    ekkfs_seek(&file, offset);

    int n = ekkfs_read(&file, entry, EKKDB_KV_ENTRY_SIZE);
    ekkfs_close(&file);

    if (n != EKKDB_KV_ENTRY_SIZE) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Write KV entry to file
 */
static int write_kv_entry(const char *filename, uint32_t index, const ekkdb_kv_entry_t *entry)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    uint32_t offset = sizeof(ekkdb_kv_header_t) + index * EKKDB_KV_ENTRY_SIZE;
    ekkfs_seek(&file, offset);

    int n = ekkfs_write(&file, entry, EKKDB_KV_ENTRY_SIZE, 0);
    ekkfs_close(&file);

    if (n != EKKDB_KV_ENTRY_SIZE) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Write KV header to file
 */
static int write_kv_header(const char *filename, const ekkdb_kv_header_t *header)
{
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, filename);
    if (ret != EKKFS_OK) {
        return EKKDB_ERR_IO;
    }

    ekkfs_seek(&file, 0);
    int n = ekkfs_write(&file, header, sizeof(ekkdb_kv_header_t), 0);
    ekkfs_close(&file);

    if (n != sizeof(ekkdb_kv_header_t)) {
        return EKKDB_ERR_IO;
    }

    return EKKDB_OK;
}

/**
 * @brief Find entry by key using linear probing
 * @return Index if found, -1 if not found, -2 if first free slot (for insert)
 */
static int find_kv_entry(ekkdb_kv_state_t *state, const char *key, uint32_t *free_slot)
{
    uint32_t hash = ekkdb_hash(key);
    uint32_t max_entries = state->header.max_entries;
    uint32_t start_idx = hash % max_entries;
    uint32_t idx = start_idx;
    int first_free = -1;

    ekkdb_kv_entry_t entry;

    do {
        int ret = read_kv_entry(state->filename, idx, &entry);
        if (ret != EKKDB_OK) {
            return -1;  /* I/O error */
        }

        if (entry.flags == EKKDB_KV_FLAG_FREE) {
            /* Found empty slot - key doesn't exist */
            if (first_free < 0) first_free = idx;
            break;
        }

        if (entry.flags == EKKDB_KV_FLAG_DELETED) {
            /* Deleted slot - could reuse for insert */
            if (first_free < 0) first_free = idx;
        } else if (entry.flags == EKKDB_KV_FLAG_USED) {
            /* Check if key matches */
            if (strncmp(entry.key, key, sizeof(entry.key)) == 0) {
                return idx;  /* Found! */
            }
        }

        idx = (idx + 1) % max_entries;
    } while (idx != start_idx);

    /* Not found */
    if (free_slot) *free_slot = first_free;
    return -1;
}

/* ============================================================================
 * Server-Side Implementation
 * ============================================================================ */

int ekkdb_kv_server_open(const char *namespace_name, uint8_t owner_id)
{
    if (!namespace_name || strlen(namespace_name) == 0) {
        return EKKDB_ERR_INVALID;
    }
    if (strlen(namespace_name) > EKKDB_KV_MAX_NAMESPACE) {
        return EKKDB_ERR_INVALID;
    }

    /* Find free handle */
    int handle = -1;
    for (int i = 0; i < EKKDB_MAX_KV_HANDLES; i++) {
        if (!g_kv_handles[i].in_use) {
            handle = i;
            break;
        }
    }
    if (handle < 0) {
        return EKKDB_ERR_FULL;
    }

    ekkdb_kv_state_t *state = &g_kv_handles[handle];
    memset(state, 0, sizeof(*state));

    /* Build filename */
    build_kv_filename(state->filename, sizeof(state->filename), namespace_name);
    strncpy(state->namespace_name, namespace_name, EKKDB_KV_MAX_NAMESPACE);
    state->owner_id = owner_id;

    /* Try to open existing file */
    ekkfs_stat_t fstat;
    int ret = ekkfs_stat(state->filename, &fstat);

    if (ret == EKKFS_OK) {
        /* File exists - read header */
        ekkfs_file_t file;
        ret = ekkfs_open(&file, state->filename);
        if (ret != EKKFS_OK) {
            return EKKDB_ERR_IO;
        }

        int n = ekkfs_read(&file, &state->header, sizeof(state->header));
        ekkfs_close(&file);

        if (n != sizeof(state->header) || state->header.magic != EKKDB_KV_MAGIC) {
            return EKKDB_ERR_CORRUPT;
        }

        /* Verify CRC */
        uint32_t crc = ekkdb_header_crc(&state->header,
                                        sizeof(state->header) - sizeof(state->header.padding));
        if (crc != state->header.crc32) {
            return EKKDB_ERR_CORRUPT;
        }
    } else {
        /* Create new file */
        ret = ekkfs_create(state->filename, owner_id, 0);
        if (ret < 0) {
            return EKKDB_ERR_IO;
        }

        /* Initialize header */
        memset(&state->header, 0, sizeof(state->header));
        state->header.magic = EKKDB_KV_MAGIC;
        state->header.version = 1;
        state->header.entry_count = 0;
        state->header.max_entries = EKKDB_KV_MAX_ENTRIES;
        state->header.owner_id = owner_id;
        state->header.created = ekkfs_get_time_us();
        state->header.modified = state->header.created;
        strncpy(state->header.namespace_name, namespace_name,
                sizeof(state->header.namespace_name) - 1);
        state->header.crc32 = ekkdb_header_crc(&state->header,
                                               sizeof(state->header) - sizeof(state->header.padding));

        /* Write header */
        ekkfs_file_t file;
        ret = ekkfs_open(&file, state->filename);
        if (ret != EKKFS_OK) {
            return EKKDB_ERR_IO;
        }

        ekkfs_write(&file, &state->header, sizeof(state->header), owner_id);

        /* Write empty entries */
        ekkdb_kv_entry_t empty;
        memset(&empty, 0, sizeof(empty));
        for (uint32_t i = 0; i < state->header.max_entries; i++) {
            ekkfs_write(&file, &empty, sizeof(empty), owner_id);
        }

        ekkfs_close(&file);
    }

    state->in_use = 1;
    state->dirty = 0;

    return handle;
}

int ekkdb_kv_server_close(uint32_t handle)
{
    if (handle >= EKKDB_MAX_KV_HANDLES || !g_kv_handles[handle].in_use) {
        return EKKDB_ERR_INVALID;
    }

    ekkdb_kv_state_t *state = &g_kv_handles[handle];

    /* Flush header if dirty */
    if (state->dirty) {
        state->header.modified = ekkfs_get_time_us();
        state->header.crc32 = ekkdb_header_crc(&state->header,
                                               sizeof(state->header) - sizeof(state->header.padding));
        write_kv_header(state->filename, &state->header);
    }

    state->in_use = 0;
    return EKKDB_OK;
}

int ekkdb_kv_server_get(uint32_t handle, const char *key, void *value, uint32_t *len)
{
    if (handle >= EKKDB_MAX_KV_HANDLES || !g_kv_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!key || strlen(key) > EKKDB_KV_MAX_KEY_LEN) {
        return EKKDB_ERR_INVALID;
    }
    if (!value || !len || *len == 0) {
        return EKKDB_ERR_INVALID;
    }

    ekkdb_kv_state_t *state = &g_kv_handles[handle];

    int idx = find_kv_entry(state, key, NULL);
    if (idx < 0) {
        return EKKDB_ERR_NOT_FOUND;
    }

    /* Read entry */
    ekkdb_kv_entry_t entry;
    int ret = read_kv_entry(state->filename, idx, &entry);
    if (ret != EKKDB_OK) {
        return ret;
    }

    /* Copy value */
    uint32_t copy_len = entry.value_len;
    if (copy_len > *len) {
        copy_len = *len;
    }
    memcpy(value, entry.value, copy_len);
    *len = entry.value_len;

    return EKKDB_OK;
}

int ekkdb_kv_server_put(uint32_t handle, const char *key, const void *value, uint32_t len)
{
    if (handle >= EKKDB_MAX_KV_HANDLES || !g_kv_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!key || strlen(key) > EKKDB_KV_MAX_KEY_LEN) {
        return EKKDB_ERR_KEY_TOO_LONG;
    }
    if (len > EKKDB_KV_MAX_VALUE_LEN) {
        return EKKDB_ERR_VALUE_TOO_BIG;
    }

    ekkdb_kv_state_t *state = &g_kv_handles[handle];
    uint32_t free_slot = 0;

    int idx = find_kv_entry(state, key, &free_slot);

    ekkdb_kv_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.key, key, sizeof(entry.key) - 1);
    entry.flags = EKKDB_KV_FLAG_USED;
    entry.value_len = len;
    if (value && len > 0) {
        memcpy(entry.value, value, len);
    }

    int ret;
    if (idx >= 0) {
        /* Update existing */
        ret = write_kv_entry(state->filename, idx, &entry);
    } else {
        /* Insert new */
        if (free_slot == (uint32_t)-1) {
            return EKKDB_ERR_FULL;
        }
        if (state->header.entry_count >= state->header.max_entries) {
            return EKKDB_ERR_FULL;
        }

        ret = write_kv_entry(state->filename, free_slot, &entry);
        if (ret == EKKDB_OK) {
            state->header.entry_count++;
            state->dirty = 1;
        }
    }

    if (ret == EKKDB_OK) {
        state->header.modified = ekkfs_get_time_us();
        state->dirty = 1;
    }

    return ret;
}

int ekkdb_kv_server_delete(uint32_t handle, const char *key)
{
    if (handle >= EKKDB_MAX_KV_HANDLES || !g_kv_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }
    if (!key || strlen(key) > EKKDB_KV_MAX_KEY_LEN) {
        return EKKDB_ERR_INVALID;
    }

    ekkdb_kv_state_t *state = &g_kv_handles[handle];

    int idx = find_kv_entry(state, key, NULL);
    if (idx < 0) {
        return EKKDB_ERR_NOT_FOUND;
    }

    /* Mark entry as deleted */
    ekkdb_kv_entry_t entry;
    int ret = read_kv_entry(state->filename, idx, &entry);
    if (ret != EKKDB_OK) {
        return ret;
    }

    entry.flags = EKKDB_KV_FLAG_DELETED;
    ret = write_kv_entry(state->filename, idx, &entry);

    if (ret == EKKDB_OK) {
        state->header.entry_count--;
        state->header.modified = ekkfs_get_time_us();
        state->dirty = 1;
    }

    return ret;
}

int ekkdb_kv_server_count(uint32_t handle)
{
    if (handle >= EKKDB_MAX_KV_HANDLES || !g_kv_handles[handle].in_use) {
        return EKKDB_ERR_NOT_OPEN;
    }

    return g_kv_handles[handle].header.entry_count;
}
