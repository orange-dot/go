/**
 * @file ekk_fs.h
 * @brief EK-KOR Filesystem Client API
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Client API for accessing EKKFS from any EK-KOR module.
 * Uses IPC message passing to communicate with the FS server on Core 3.
 *
 * Usage:
 *   // Wait for FS server to be ready
 *   while (!ekk_fs_is_ready()) {
 *       ekk_hal_delay_us(1000);
 *   }
 *
 *   // Create and write a file
 *   int fd = ekk_fs_create("config.txt", 0);
 *   if (fd >= 0) {
 *       ekk_fs_write(fd, "key=value\n", 10);
 *       ekk_fs_close(fd);
 *   }
 *
 *   // Open and read a file
 *   fd = ekk_fs_open("config.txt");
 *   if (fd >= 0) {
 *       char buf[64];
 *       int n = ekk_fs_read(fd, buf, sizeof(buf));
 *       ekk_fs_close(fd);
 *   }
 */

#ifndef EKK_FS_H
#define EKK_FS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Codes (match EKKFS codes)
 * ============================================================================ */

#define EKK_FS_OK               0
#define EKK_FS_ERR_IO           -1
#define EKK_FS_ERR_CORRUPT      -2
#define EKK_FS_ERR_NOT_FOUND    -3
#define EKK_FS_ERR_EXISTS       -4
#define EKK_FS_ERR_FULL         -5
#define EKK_FS_ERR_NO_INODES    -6
#define EKK_FS_ERR_INVALID      -7
#define EKK_FS_ERR_NOT_MOUNTED  -8
#define EKK_FS_ERR_NAME_TOO_LONG -9
#define EKK_FS_ERR_PERMISSION   -10
#define EKK_FS_ERR_TIMEOUT      -11
#define EKK_FS_ERR_NOT_READY    -12

/* ============================================================================
 * File Flags
 * ============================================================================ */

#define EKK_FS_FLAG_SYSTEM      (1 << 1)    /* System file (protected) */
#define EKK_FS_FLAG_LOG         (1 << 2)    /* Log file (append-only) */
#define EKK_FS_FLAG_MODULE      (1 << 3)    /* Module image */

/* ============================================================================
 * File Information
 * ============================================================================ */

/**
 * @brief File information structure
 */
typedef struct {
    uint32_t inode_num;         /* Inode number */
    uint32_t flags;             /* File flags */
    uint32_t owner_id;          /* Owner module ID */
    uint32_t size;              /* File size in bytes */
    uint64_t created;           /* Creation time (microseconds) */
    uint64_t modified;          /* Modification time (microseconds) */
    char     name[16];          /* Filename */
} ekk_fs_stat_t;

/**
 * @brief Filesystem statistics
 */
typedef struct {
    uint32_t total_blocks;      /* Total blocks in filesystem */
    uint32_t free_blocks;       /* Free blocks available */
    uint32_t total_inodes;      /* Total inode slots */
    uint32_t used_inodes;       /* Used inode slots */
} ekk_fs_statfs_t;

/* ============================================================================
 * Client API Functions
 * ============================================================================ */

/**
 * @brief Check if filesystem server is ready
 *
 * @return 1 if ready, 0 if not yet initialized
 */
int ekk_fs_is_ready(void);

/**
 * @brief Create a new file
 *
 * @param name Filename (max 15 characters)
 * @param flags Initial flags (EKK_FS_FLAG_*)
 * @return File handle (>=0) on success, negative error code on failure
 */
int ekk_fs_create(const char *name, uint32_t flags);

/**
 * @brief Delete a file
 *
 * Caller must own the file or be system (module ID 0).
 *
 * @param name Filename
 * @return EKK_FS_OK on success, negative error code on failure
 */
int ekk_fs_delete(const char *name);

/**
 * @brief Open an existing file
 *
 * @param name Filename
 * @return File handle (>=0) on success, negative error code on failure
 */
int ekk_fs_open(const char *name);

/**
 * @brief Close a file
 *
 * @param fd File handle
 * @return EKK_FS_OK on success
 */
int ekk_fs_close(int fd);

/**
 * @brief Read from a file
 *
 * Reads up to `size` bytes starting at the current file position.
 * The file position is advanced by the number of bytes read.
 *
 * @param fd File handle
 * @param buffer Destination buffer
 * @param size Maximum bytes to read
 * @return Number of bytes read (>=0), or negative error code
 */
int ekk_fs_read(int fd, void *buffer, size_t size);

/**
 * @brief Write to a file
 *
 * Writes up to `size` bytes starting at the current file position.
 * The file position is advanced by the number of bytes written.
 *
 * @param fd File handle
 * @param buffer Source buffer
 * @param size Number of bytes to write
 * @return Number of bytes written (>=0), or negative error code
 */
int ekk_fs_write(int fd, const void *buffer, size_t size);

/**
 * @brief Seek to position in file
 *
 * @param fd File handle
 * @param position New file position (bytes from start)
 * @return New position on success, negative error code on failure
 */
int ekk_fs_seek(int fd, uint32_t position);

/**
 * @brief Get file information
 *
 * @param name Filename
 * @param stat Pointer to stat structure to fill
 * @return EKK_FS_OK on success, negative error code on failure
 */
int ekk_fs_stat(const char *name, ekk_fs_stat_t *stat);

/**
 * @brief Sync filesystem to disk
 *
 * Flushes all cached data to the SD card.
 *
 * @return EKK_FS_OK on success, negative error code on failure
 */
int ekk_fs_sync(void);

/**
 * @brief Get filesystem statistics
 *
 * @param statfs Pointer to statfs structure to fill
 * @return EKK_FS_OK on success, negative error code on failure
 */
int ekk_fs_statfs(ekk_fs_statfs_t *statfs);

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/**
 * @brief Read entire file into buffer
 *
 * Opens the file, reads entire contents, and closes it.
 *
 * @param name Filename
 * @param buffer Destination buffer
 * @param max_size Maximum bytes to read
 * @return Number of bytes read (>=0), or negative error code
 */
int ekk_fs_read_file(const char *name, void *buffer, size_t max_size);

/**
 * @brief Write entire file from buffer
 *
 * Creates/overwrites the file with the provided data.
 *
 * @param name Filename
 * @param buffer Source buffer
 * @param size Number of bytes to write
 * @param flags File flags (for new files)
 * @return EKK_FS_OK on success, negative error code on failure
 */
int ekk_fs_write_file(const char *name, const void *buffer, size_t size, uint32_t flags);

/**
 * @brief Append data to a file
 *
 * Opens the file, seeks to end, writes data, and closes it.
 *
 * @param name Filename
 * @param buffer Data to append
 * @param size Number of bytes to append
 * @return Number of bytes written (>=0), or negative error code
 */
int ekk_fs_append(const char *name, const void *buffer, size_t size);

/**
 * @brief Check if a file exists
 *
 * @param name Filename
 * @return 1 if file exists, 0 if not
 */
int ekk_fs_exists(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* EKK_FS_H */
