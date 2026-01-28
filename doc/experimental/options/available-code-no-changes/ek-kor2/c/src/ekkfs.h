/**
 * @file ekkfs.h
 * @brief EKKFS - EK-KOR Filesystem Core
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Custom filesystem designed for EK-KOR2 RTOS microkernel architecture.
 * Simple, reliable filesystem for config, logs, module images, and field state.
 *
 * Features:
 * - Flat namespace (no directories)
 * - Fixed 512-byte blocks
 * - CRC32 integrity checking
 * - Module ownership (owner_id)
 * - Journal for atomicity
 */

#ifndef EKKFS_H
#define EKKFS_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define EKKFS_MAGIC             0x454B4653  /* "EKFS" */
#define EKKFS_VERSION           1
#define EKKFS_BLOCK_SIZE        512
#define EKKFS_MAX_FILENAME      15          /* 15 chars + null terminator */
#define EKKFS_DIRECT_BLOCKS     10          /* Direct block pointers per inode */

/* Default configuration (can be overridden at format time) */
#define EKKFS_DEFAULT_INODES    256         /* Max files */
#define EKKFS_INODES_PER_BLOCK  8           /* 64 bytes per inode, 8 per block */

/* ============================================================================
 * Error Codes
 * ============================================================================ */

#define EKKFS_OK                0
#define EKKFS_ERR_IO            -1          /* SD I/O error */
#define EKKFS_ERR_CORRUPT       -2          /* Filesystem corrupted */
#define EKKFS_ERR_NOT_FOUND     -3          /* File not found */
#define EKKFS_ERR_EXISTS        -4          /* File already exists */
#define EKKFS_ERR_FULL          -5          /* Filesystem full */
#define EKKFS_ERR_NO_INODES     -6          /* No free inodes */
#define EKKFS_ERR_INVALID       -7          /* Invalid argument */
#define EKKFS_ERR_NOT_MOUNTED   -8          /* Filesystem not mounted */
#define EKKFS_ERR_NAME_TOO_LONG -9          /* Filename too long */
#define EKKFS_ERR_PERMISSION    -10         /* Permission denied (wrong owner) */

/* ============================================================================
 * Inode Flags
 * ============================================================================ */

#define EKKFS_FLAG_USED         (1 << 0)    /* Inode is in use */
#define EKKFS_FLAG_SYSTEM       (1 << 1)    /* System file (protected) */
#define EKKFS_FLAG_LOG          (1 << 2)    /* Log file (append-only) */
#define EKKFS_FLAG_MODULE       (1 << 3)    /* Module image */

/* ============================================================================
 * On-Disk Structures
 * ============================================================================ */

/**
 * @brief Superblock (512 bytes)
 *
 * Located at block 0 of the EKKFS partition.
 */
typedef struct {
    uint32_t magic;             /* EKKFS_MAGIC */
    uint32_t version;           /* EKKFS_VERSION */
    uint32_t block_size;        /* Always 512 */
    uint32_t total_blocks;      /* Total blocks in partition */
    uint32_t inode_count;       /* Max number of inodes */
    uint32_t inode_start;       /* Block number of first inode block */
    uint32_t bitmap_start;      /* Block number of free block bitmap */
    uint32_t journal_start;     /* Block number of journal */
    uint32_t data_start;        /* Block number of first data block */
    uint32_t free_blocks;       /* Number of free data blocks */
    uint64_t mount_time;        /* Last mount timestamp (microseconds) */
    uint32_t mount_count;       /* Number of times mounted */
    uint32_t crc32;             /* CRC32 of superblock (excluding this field) */
    uint8_t  reserved[456];     /* Padding to 512 bytes */
} __attribute__((packed)) ekkfs_superblock_t;

/**
 * @brief Inode (64 bytes)
 *
 * 8 inodes fit in one 512-byte block.
 */
typedef struct {
    uint32_t flags;             /* EKKFS_FLAG_* */
    uint32_t owner_id;          /* EKK module ID (0=system, 1-255=module) */
    uint32_t size;              /* File size in bytes */
    uint32_t blocks[EKKFS_DIRECT_BLOCKS];  /* Direct block pointers */
    uint32_t indirect;          /* Indirect block pointer (for large files) */
    uint64_t created;           /* Creation time (microseconds since epoch) */
    uint64_t modified;          /* Modification time */
    uint32_t crc32;             /* CRC32 of file data */
    char     name[16];          /* Null-terminated filename */
} __attribute__((packed)) ekkfs_inode_t;

/**
 * @brief Journal entry (32 bytes)
 *
 * Used for atomic operations. 16 entries per block.
 */
typedef struct {
    uint32_t sequence;          /* Sequence number */
    uint32_t type;              /* Operation type */
    uint32_t inode;             /* Affected inode */
    uint32_t block;             /* Affected block */
    uint32_t old_value;         /* Previous value */
    uint32_t new_value;         /* New value */
    uint32_t timestamp;         /* Time of operation */
    uint32_t crc32;             /* Entry CRC */
} __attribute__((packed)) ekkfs_journal_entry_t;

/* Journal operation types */
#define EKKFS_JOURNAL_NOP           0   /* No operation (empty slot) */
#define EKKFS_JOURNAL_CREATE        1   /* File creation */
#define EKKFS_JOURNAL_DELETE        2   /* File deletion */
#define EKKFS_JOURNAL_WRITE         3   /* Data write */
#define EKKFS_JOURNAL_TRUNCATE      4   /* File truncation */
#define EKKFS_JOURNAL_ALLOC_BLOCK   5   /* Block allocation */
#define EKKFS_JOURNAL_FREE_BLOCK    6   /* Block free */
#define EKKFS_JOURNAL_COMMIT        7   /* Transaction commit marker */

/* Journal header (first entry in journal block) */
typedef struct {
    uint32_t magic;             /* EKKFS_JOURNAL_MAGIC */
    uint32_t head;              /* Next write position */
    uint32_t tail;              /* Oldest valid entry */
    uint32_t sequence;          /* Current sequence number */
    uint32_t tx_active;         /* Transaction in progress? */
    uint32_t tx_start_seq;      /* Transaction start sequence */
    uint32_t reserved[2];
    uint32_t crc32;             /* Header CRC */
    uint8_t  padding[24];       /* Pad to 64 bytes */
} __attribute__((packed)) ekkfs_journal_header_t;

#define EKKFS_JOURNAL_MAGIC         0x4A524E4C  /* "JRNL" */
#define EKKFS_JOURNAL_ENTRIES       15          /* Entries per block (after header) */
#define EKKFS_JOURNAL_BLOCKS        4           /* Number of journal blocks */

/* ============================================================================
 * Runtime Structures
 * ============================================================================ */

/**
 * @brief File handle for open files
 */
typedef struct {
    uint32_t inode_num;         /* Inode number */
    uint32_t position;          /* Current read/write position */
    uint32_t flags;             /* Open flags */
} ekkfs_file_t;

/**
 * @brief File information (stat)
 */
typedef struct {
    uint32_t inode_num;
    uint32_t flags;
    uint32_t owner_id;
    uint32_t size;
    uint64_t created;
    uint64_t modified;
    char     name[16];
} ekkfs_stat_t;

/**
 * @brief Journal state (in RAM)
 */
typedef struct {
    ekkfs_journal_header_t header;
    uint32_t current_tx_seq;    /* Current transaction sequence */
    int      tx_active;         /* Transaction in progress */
    int      dirty;             /* Journal needs flush */
} ekkfs_journal_state_t;

/**
 * @brief Filesystem state
 */
typedef struct {
    int      mounted;           /* 1 if mounted */
    uint32_t partition_lba;     /* Partition start LBA */
    ekkfs_superblock_t superblock;
    uint32_t *bitmap;           /* Free block bitmap (in RAM) */
    uint32_t bitmap_blocks;     /* Number of blocks in bitmap */
    ekkfs_journal_state_t journal;  /* Journal state */
} ekkfs_state_t;

/* ============================================================================
 * Filesystem Operations
 * ============================================================================ */

/**
 * @brief Format a partition as EKKFS
 *
 * @param partition_lba Starting LBA of the partition
 * @param total_blocks Total blocks in the partition
 * @param inode_count Maximum number of files (default 256)
 * @return EKKFS_OK on success, error code on failure
 */
int ekkfs_format(uint32_t partition_lba, uint32_t total_blocks, uint32_t inode_count);

/**
 * @brief Mount an EKKFS partition
 *
 * @param partition_lba Starting LBA of the partition
 * @return EKKFS_OK on success, error code on failure
 */
int ekkfs_mount(uint32_t partition_lba);

/**
 * @brief Unmount the filesystem
 *
 * @return EKKFS_OK on success
 */
int ekkfs_unmount(void);

/**
 * @brief Check if filesystem is mounted
 *
 * @return 1 if mounted, 0 if not
 */
int ekkfs_is_mounted(void);

/**
 * @brief Sync filesystem (flush all caches)
 *
 * @return EKKFS_OK on success
 */
int ekkfs_sync(void);

/* ============================================================================
 * File Operations
 * ============================================================================ */

/**
 * @brief Create a new file
 *
 * @param name Filename (max 15 characters)
 * @param owner_id Module ID of the owner (0 for system)
 * @param flags Initial flags (EKKFS_FLAG_*)
 * @return Inode number on success, negative error code on failure
 */
int ekkfs_create(const char *name, uint32_t owner_id, uint32_t flags);

/**
 * @brief Delete a file
 *
 * @param name Filename
 * @param owner_id Requesting owner (must match or be 0)
 * @return EKKFS_OK on success, error code on failure
 */
int ekkfs_delete(const char *name, uint32_t owner_id);

/**
 * @brief Open a file
 *
 * @param file File handle to initialize
 * @param name Filename
 * @return EKKFS_OK on success, error code on failure
 */
int ekkfs_open(ekkfs_file_t *file, const char *name);

/**
 * @brief Close a file
 *
 * @param file File handle
 * @return EKKFS_OK on success
 */
int ekkfs_close(ekkfs_file_t *file);

/**
 * @brief Read from a file
 *
 * @param file File handle
 * @param buffer Destination buffer
 * @param size Number of bytes to read
 * @return Number of bytes read, or negative error code
 */
int ekkfs_read(ekkfs_file_t *file, void *buffer, uint32_t size);

/**
 * @brief Write to a file
 *
 * @param file File handle
 * @param buffer Source buffer
 * @param size Number of bytes to write
 * @param owner_id Requesting owner (must match file owner or be 0)
 * @return Number of bytes written, or negative error code
 */
int ekkfs_write(ekkfs_file_t *file, const void *buffer, uint32_t size, uint32_t owner_id);

/**
 * @brief Seek to position in file
 *
 * @param file File handle
 * @param position New position
 * @return EKKFS_OK on success
 */
int ekkfs_seek(ekkfs_file_t *file, uint32_t position);

/**
 * @brief Get file information
 *
 * @param name Filename
 * @param stat Stat structure to fill
 * @return EKKFS_OK on success, error code on failure
 */
int ekkfs_stat(const char *name, ekkfs_stat_t *stat);

/* ============================================================================
 * Directory Operations
 * ============================================================================ */

/**
 * @brief List all files
 *
 * @param callback Called for each file with (inode_num, name, size, owner_id)
 * @param user_data Passed to callback
 * @return Number of files listed, or negative error code
 */
int ekkfs_list(void (*callback)(uint32_t inode, const char *name, uint32_t size,
                                uint32_t owner, void *user_data), void *user_data);

/**
 * @brief Get filesystem statistics
 *
 * @param total_blocks Total blocks (output)
 * @param free_blocks Free blocks (output)
 * @param total_inodes Total inodes (output)
 * @param used_inodes Used inodes (output)
 * @return EKKFS_OK on success
 */
int ekkfs_statfs(uint32_t *total_blocks, uint32_t *free_blocks,
                 uint32_t *total_inodes, uint32_t *used_inodes);

/* ============================================================================
 * Low-Level Utilities
 * ============================================================================ */

/**
 * @brief Calculate CRC32 of a buffer
 *
 * @param data Buffer
 * @param len Length
 * @return CRC32 value
 */
uint32_t ekkfs_crc32(const void *data, size_t len);

/**
 * @brief Get block cache statistics
 *
 * @param hits Pointer to store cache hit count (or NULL)
 * @param misses Pointer to store cache miss count (or NULL)
 */
void ekkfs_cache_stats(uint32_t *hits, uint32_t *misses);

/**
 * @brief Get current timestamp (microseconds)
 *
 * Provided by HAL, declared here for filesystem use.
 *
 * @return Current time in microseconds
 */
uint64_t ekkfs_get_time_us(void);

/* ============================================================================
 * Journal Operations
 * ============================================================================ */

/**
 * @brief Initialize journal (called during format)
 *
 * @return EKKFS_OK on success
 */
int ekkfs_journal_init(void);

/**
 * @brief Recover journal (called during mount)
 *
 * Replays committed transactions and rolls back incomplete ones.
 *
 * @return EKKFS_OK on success
 */
int ekkfs_journal_recover(void);

/**
 * @brief Begin a transaction
 *
 * All operations until commit/abort are part of this transaction.
 *
 * @return Transaction ID (>0) or error
 */
int ekkfs_tx_begin(void);

/**
 * @brief Commit current transaction
 *
 * Makes all changes permanent.
 *
 * @return EKKFS_OK on success
 */
int ekkfs_tx_commit(void);

/**
 * @brief Abort current transaction
 *
 * Discards all changes since tx_begin.
 *
 * @return EKKFS_OK on success
 */
int ekkfs_tx_abort(void);

/**
 * @brief Log a journal entry
 *
 * @param type Operation type (EKKFS_JOURNAL_*)
 * @param inode Affected inode
 * @param block Affected block
 * @param old_value Previous value
 * @param new_value New value
 * @return EKKFS_OK on success
 */
int ekkfs_journal_log(uint32_t type, uint32_t inode, uint32_t block,
                      uint32_t old_value, uint32_t new_value);

#endif /* EKKFS_H */
