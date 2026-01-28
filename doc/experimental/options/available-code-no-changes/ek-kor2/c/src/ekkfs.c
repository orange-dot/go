/**
 * @file ekkfs.c
 * @brief EKKFS - EK-KOR Filesystem Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#include "ekkfs.h"
#include "hal/rpi3/sd.h"
#include "ekk/ekk_hal.h"
#include <string.h>

/* Debug output */
#define EKKFS_DEBUG             1

#if EKKFS_DEBUG
#include "hal/rpi3/uart.h"
#define fs_debug(...)           uart_printf(__VA_ARGS__)
#else
#define fs_debug(...)
#endif

/* ============================================================================
 * Static State
 * ============================================================================ */

static ekkfs_state_t g_fs;

/* Block buffer for I/O operations */
static uint8_t g_block_buf[EKKFS_BLOCK_SIZE] __attribute__((aligned(16)));

/* Bitmap buffer (statically allocated for simplicity) */
#define MAX_BITMAP_BLOCKS       8   /* Supports up to 32MB partition */
static uint8_t g_bitmap_buf[MAX_BITMAP_BLOCKS * EKKFS_BLOCK_SIZE] __attribute__((aligned(16)));

/* ============================================================================
 * Block Cache
 * ============================================================================ */

#define CACHE_SIZE              16  /* Number of cached blocks */
#define CACHE_INVALID_BLOCK     0xFFFFFFFF

typedef struct {
    uint32_t block_num;         /* Block number (or CACHE_INVALID_BLOCK if empty) */
    uint32_t access_count;      /* LRU counter */
    int      dirty;             /* Needs write-back */
    uint8_t  data[EKKFS_BLOCK_SIZE];
} cache_entry_t;

typedef struct {
    cache_entry_t entries[CACHE_SIZE];
    uint32_t access_counter;    /* Global access counter for LRU */
    uint32_t hits;              /* Cache hit counter */
    uint32_t misses;            /* Cache miss counter */
    int      enabled;           /* Cache enabled flag */
} block_cache_t;

static block_cache_t g_cache;

/**
 * @brief Initialize the block cache
 */
static void cache_init(void)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        g_cache.entries[i].block_num = CACHE_INVALID_BLOCK;
        g_cache.entries[i].access_count = 0;
        g_cache.entries[i].dirty = 0;
    }
    g_cache.access_counter = 0;
    g_cache.hits = 0;
    g_cache.misses = 0;
    g_cache.enabled = 1;
    fs_debug("EKKFS: Block cache initialized (%d entries)\n", CACHE_SIZE);
}

/**
 * @brief Find a block in cache
 * @return Cache entry index, or -1 if not found
 */
static int cache_find(uint32_t block_num)
{
    if (!g_cache.enabled) return -1;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (g_cache.entries[i].block_num == block_num) {
            g_cache.entries[i].access_count = ++g_cache.access_counter;
            g_cache.hits++;
            return i;
        }
    }
    g_cache.misses++;
    return -1;
}

/**
 * @brief Find LRU (least recently used) cache entry
 * @return Cache entry index
 */
static int cache_find_lru(void)
{
    int lru_idx = 0;
    uint32_t min_access = g_cache.entries[0].access_count;

    for (int i = 1; i < CACHE_SIZE; i++) {
        /* Prefer empty slots */
        if (g_cache.entries[i].block_num == CACHE_INVALID_BLOCK) {
            return i;
        }
        if (g_cache.entries[i].access_count < min_access) {
            min_access = g_cache.entries[i].access_count;
            lru_idx = i;
        }
    }
    return lru_idx;
}

/**
 * @brief Write back a dirty cache entry
 * @return SD_OK on success
 */
static int cache_writeback(int idx)
{
    cache_entry_t *entry = &g_cache.entries[idx];
    if (entry->dirty && entry->block_num != CACHE_INVALID_BLOCK) {
        uint32_t lba = g_fs.partition_lba + entry->block_num;
        int ret = sd_write_block(lba, entry->data);
        if (ret == SD_OK) {
            entry->dirty = 0;
        }
        return ret;
    }
    return SD_OK;
}

/**
 * @brief Flush all dirty cache entries
 * @return SD_OK on success
 */
static int cache_flush(void)
{
    int result = SD_OK;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (g_cache.entries[i].dirty) {
            int ret = cache_writeback(i);
            if (ret != SD_OK) result = ret;
        }
    }
    return result;
}

/**
 * @brief Invalidate entire cache
 */
static void cache_invalidate(void)
{
    cache_flush();  /* Write back dirty entries first */
    for (int i = 0; i < CACHE_SIZE; i++) {
        g_cache.entries[i].block_num = CACHE_INVALID_BLOCK;
        g_cache.entries[i].dirty = 0;
    }
}

/**
 * @brief Get cache statistics
 */
void ekkfs_cache_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits) *hits = g_cache.hits;
    if (misses) *misses = g_cache.misses;
}

/* ============================================================================
 * CRC32 Implementation (polynomial 0xEDB88320)
 * ============================================================================ */

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t ekkfs_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

/* ============================================================================
 * Time Function (weak, can be overridden by HAL)
 * ============================================================================ */

__attribute__((weak)) uint64_t ekkfs_get_time_us(void)
{
    return ekk_hal_time_us();
}

/* ============================================================================
 * Low-Level Block I/O (with caching)
 * ============================================================================ */

static int read_block(uint32_t block_num, void *buffer)
{
    /* Check cache first */
    int idx = cache_find(block_num);
    if (idx >= 0) {
        memcpy(buffer, g_cache.entries[idx].data, EKKFS_BLOCK_SIZE);
        return SD_OK;
    }

    /* Cache miss - read from SD */
    uint32_t lba = g_fs.partition_lba + block_num;
    int ret = sd_read_block(lba, buffer);
    if (ret != SD_OK) {
        return ret;
    }

    /* Add to cache */
    if (g_cache.enabled) {
        idx = cache_find_lru();
        /* Write back old entry if dirty */
        cache_writeback(idx);
        /* Store new entry */
        g_cache.entries[idx].block_num = block_num;
        g_cache.entries[idx].access_count = ++g_cache.access_counter;
        g_cache.entries[idx].dirty = 0;
        memcpy(g_cache.entries[idx].data, buffer, EKKFS_BLOCK_SIZE);
    }

    return SD_OK;
}

static int write_block(uint32_t block_num, const void *buffer)
{
    /* Check if block is in cache */
    int idx = cache_find(block_num);

    if (idx >= 0) {
        /* Update cache entry */
        memcpy(g_cache.entries[idx].data, buffer, EKKFS_BLOCK_SIZE);
        g_cache.entries[idx].dirty = 1;
        g_cache.entries[idx].access_count = ++g_cache.access_counter;
        /* Write-through for reliability */
        uint32_t lba = g_fs.partition_lba + block_num;
        return sd_write_block(lba, buffer);
    }

    /* Not in cache - write directly and add to cache */
    uint32_t lba = g_fs.partition_lba + block_num;
    int ret = sd_write_block(lba, buffer);
    if (ret != SD_OK) {
        return ret;
    }

    /* Add to cache (write-allocate) */
    if (g_cache.enabled) {
        idx = cache_find_lru();
        cache_writeback(idx);
        g_cache.entries[idx].block_num = block_num;
        g_cache.entries[idx].access_count = ++g_cache.access_counter;
        g_cache.entries[idx].dirty = 0;  /* Already written to disk */
        memcpy(g_cache.entries[idx].data, buffer, EKKFS_BLOCK_SIZE);
    }

    return SD_OK;
}

/* ============================================================================
 * Bitmap Operations
 * ============================================================================ */

static int bitmap_get(uint32_t block_num)
{
    if (block_num < g_fs.superblock.data_start) {
        return 1;  /* Metadata blocks are always "used" */
    }

    uint32_t data_block = block_num - g_fs.superblock.data_start;
    uint32_t byte_idx = data_block / 8;
    uint32_t bit_idx = data_block % 8;

    return (g_bitmap_buf[byte_idx] >> bit_idx) & 1;
}

static void bitmap_set(uint32_t block_num, int used)
{
    if (block_num < g_fs.superblock.data_start) {
        return;
    }

    uint32_t data_block = block_num - g_fs.superblock.data_start;
    uint32_t byte_idx = data_block / 8;
    uint32_t bit_idx = data_block % 8;

    if (used) {
        g_bitmap_buf[byte_idx] |= (1 << bit_idx);
    } else {
        g_bitmap_buf[byte_idx] &= ~(1 << bit_idx);
    }
}

/* Forward declaration for journal logging */
int ekkfs_journal_log(uint32_t type, uint32_t inode, uint32_t block,
                      uint32_t old_value, uint32_t new_value);

static int bitmap_alloc(void)
{
    uint32_t total_data_blocks = g_fs.superblock.total_blocks - g_fs.superblock.data_start;
    uint32_t bitmap_bytes = (total_data_blocks + 7) / 8;

    for (uint32_t i = 0; i < bitmap_bytes; i++) {
        if (g_bitmap_buf[i] != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                if (!(g_bitmap_buf[i] & (1 << bit))) {
                    uint32_t block = g_fs.superblock.data_start + i * 8 + bit;
                    if (block < g_fs.superblock.total_blocks) {
                        g_bitmap_buf[i] |= (1 << bit);
                        g_fs.superblock.free_blocks--;
                        /* Log block allocation to journal */
                        if (g_fs.journal.tx_active) {
                            ekkfs_journal_log(EKKFS_JOURNAL_ALLOC_BLOCK, 0, block, 0, block);
                        }
                        return (int)block;
                    }
                }
            }
        }
    }

    return -1;  /* No free blocks */
}

static void bitmap_free(uint32_t block_num)
{
    if (block_num >= g_fs.superblock.data_start) {
        /* Log block free to journal */
        if (g_fs.journal.tx_active) {
            ekkfs_journal_log(EKKFS_JOURNAL_FREE_BLOCK, 0, block_num, block_num, 0);
        }
        bitmap_set(block_num, 0);
        g_fs.superblock.free_blocks++;
    }
}

static int bitmap_load(void)
{
    for (uint32_t i = 0; i < g_fs.bitmap_blocks; i++) {
        if (read_block(g_fs.superblock.bitmap_start + i,
                       g_bitmap_buf + i * EKKFS_BLOCK_SIZE) != SD_OK) {
            return EKKFS_ERR_IO;
        }
    }
    return EKKFS_OK;
}

static int bitmap_save(void)
{
    for (uint32_t i = 0; i < g_fs.bitmap_blocks; i++) {
        if (write_block(g_fs.superblock.bitmap_start + i,
                        g_bitmap_buf + i * EKKFS_BLOCK_SIZE) != SD_OK) {
            return EKKFS_ERR_IO;
        }
    }
    return EKKFS_OK;
}

/* ============================================================================
 * Inode Operations
 * ============================================================================ */

static int inode_read(uint32_t inode_num, ekkfs_inode_t *inode)
{
    if (inode_num >= g_fs.superblock.inode_count) {
        return EKKFS_ERR_INVALID;
    }

    uint32_t block = g_fs.superblock.inode_start + (inode_num / EKKFS_INODES_PER_BLOCK);
    uint32_t offset = (inode_num % EKKFS_INODES_PER_BLOCK) * sizeof(ekkfs_inode_t);

    if (read_block(block, g_block_buf) != SD_OK) {
        return EKKFS_ERR_IO;
    }

    memcpy(inode, g_block_buf + offset, sizeof(ekkfs_inode_t));
    return EKKFS_OK;
}

static int inode_write(uint32_t inode_num, const ekkfs_inode_t *inode)
{
    if (inode_num >= g_fs.superblock.inode_count) {
        return EKKFS_ERR_INVALID;
    }

    uint32_t block = g_fs.superblock.inode_start + (inode_num / EKKFS_INODES_PER_BLOCK);
    uint32_t offset = (inode_num % EKKFS_INODES_PER_BLOCK) * sizeof(ekkfs_inode_t);

    if (read_block(block, g_block_buf) != SD_OK) {
        return EKKFS_ERR_IO;
    }

    memcpy(g_block_buf + offset, inode, sizeof(ekkfs_inode_t));

    if (write_block(block, g_block_buf) != SD_OK) {
        return EKKFS_ERR_IO;
    }

    return EKKFS_OK;
}

static int inode_alloc(void)
{
    ekkfs_inode_t inode;

    for (uint32_t i = 0; i < g_fs.superblock.inode_count; i++) {
        if (inode_read(i, &inode) != EKKFS_OK) {
            continue;
        }
        if (!(inode.flags & EKKFS_FLAG_USED)) {
            return (int)i;
        }
    }

    return -1;  /* No free inodes */
}

static int inode_find_by_name(const char *name)
{
    ekkfs_inode_t inode;

    for (uint32_t i = 0; i < g_fs.superblock.inode_count; i++) {
        if (inode_read(i, &inode) != EKKFS_OK) {
            continue;
        }
        if ((inode.flags & EKKFS_FLAG_USED) && strncmp(inode.name, name, EKKFS_MAX_FILENAME) == 0) {
            return (int)i;
        }
    }

    return -1;  /* Not found */
}

/* ============================================================================
 * Filesystem Operations
 * ============================================================================ */

int ekkfs_format(uint32_t partition_lba, uint32_t total_blocks, uint32_t inode_count)
{
    fs_debug("EKKFS: Formatting partition at LBA %lu, %lu blocks, %lu inodes\n",
             partition_lba, total_blocks, inode_count);

    if (inode_count == 0) {
        inode_count = EKKFS_DEFAULT_INODES;
    }

    /* Calculate layout */
    uint32_t inode_blocks = (inode_count + EKKFS_INODES_PER_BLOCK - 1) / EKKFS_INODES_PER_BLOCK;
    uint32_t journal_blocks = EKKFS_JOURNAL_BLOCKS;  /* 4 blocks for journal */
    uint32_t data_blocks = total_blocks - 1 - inode_blocks - 1 - journal_blocks;  /* -superblock -inodes -bitmap -journal */
    uint32_t bitmap_blocks = (data_blocks + (8 * EKKFS_BLOCK_SIZE) - 1) / (8 * EKKFS_BLOCK_SIZE);

    /* Recalculate data blocks after bitmap allocation */
    data_blocks = total_blocks - 1 - inode_blocks - bitmap_blocks - journal_blocks;

    /* Build superblock */
    ekkfs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = EKKFS_MAGIC;
    sb.version = EKKFS_VERSION;
    sb.block_size = EKKFS_BLOCK_SIZE;
    sb.total_blocks = total_blocks;
    sb.inode_count = inode_count;
    sb.inode_start = 1;
    sb.bitmap_start = 1 + inode_blocks;
    sb.journal_start = sb.bitmap_start + bitmap_blocks;
    sb.data_start = sb.journal_start + journal_blocks;
    sb.free_blocks = data_blocks;
    sb.mount_time = ekkfs_get_time_us();
    sb.mount_count = 0;
    sb.crc32 = ekkfs_crc32(&sb, offsetof(ekkfs_superblock_t, crc32));

    fs_debug("EKKFS: Layout: inodes@%lu, bitmap@%lu, journal@%lu, data@%lu\n",
             sb.inode_start, sb.bitmap_start, sb.journal_start, sb.data_start);
    fs_debug("EKKFS: inode_blocks=%lu, bitmap_blocks=%lu, data_blocks=%lu\n",
             inode_blocks, bitmap_blocks, data_blocks);

    /* Write superblock */
    memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
    memcpy(g_block_buf, &sb, sizeof(sb));
    if (sd_write_block(partition_lba, g_block_buf) != SD_OK) {
        fs_debug("EKKFS: Failed to write superblock\n");
        return EKKFS_ERR_IO;
    }

    /* Write empty inode blocks */
    memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < inode_blocks; i++) {
        if (sd_write_block(partition_lba + sb.inode_start + i, g_block_buf) != SD_OK) {
            fs_debug("EKKFS: Failed to write inode block %lu\n", i);
            return EKKFS_ERR_IO;
        }
    }

    /* Write bitmap (all zeros = all free) */
    memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < bitmap_blocks; i++) {
        if (sd_write_block(partition_lba + sb.bitmap_start + i, g_block_buf) != SD_OK) {
            fs_debug("EKKFS: Failed to write bitmap block %lu\n", i);
            return EKKFS_ERR_IO;
        }
    }

    /* Initialize journal with proper header */
    ekkfs_journal_header_t jh;
    memset(&jh, 0, sizeof(jh));
    jh.magic = EKKFS_JOURNAL_MAGIC;
    jh.head = 0;
    jh.tail = 0;
    jh.sequence = 1;
    jh.tx_active = 0;
    jh.tx_start_seq = 0;
    jh.crc32 = ekkfs_crc32(&jh, offsetof(ekkfs_journal_header_t, crc32));

    /* Write journal header block */
    memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
    memcpy(g_block_buf, &jh, sizeof(jh));
    if (sd_write_block(partition_lba + sb.journal_start, g_block_buf) != SD_OK) {
        fs_debug("EKKFS: Failed to write journal header\n");
        return EKKFS_ERR_IO;
    }

    /* Clear remaining journal blocks */
    memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
    for (uint32_t i = 1; i < journal_blocks; i++) {
        if (sd_write_block(partition_lba + sb.journal_start + i, g_block_buf) != SD_OK) {
            fs_debug("EKKFS: Failed to write journal block %lu\n", i);
            return EKKFS_ERR_IO;
        }
    }

    fs_debug("EKKFS: Format complete\n");
    return EKKFS_OK;
}

int ekkfs_mount(uint32_t partition_lba)
{
    fs_debug("EKKFS: Mounting partition at LBA %lu\n", partition_lba);

    /* Read superblock */
    if (sd_read_block(partition_lba, g_block_buf) != SD_OK) {
        fs_debug("EKKFS: Failed to read superblock\n");
        return EKKFS_ERR_IO;
    }

    ekkfs_superblock_t *sb = (ekkfs_superblock_t *)g_block_buf;

    /* Validate magic */
    if (sb->magic != EKKFS_MAGIC) {
        fs_debug("EKKFS: Invalid magic: 0x%08lx\n", sb->magic);
        return EKKFS_ERR_CORRUPT;
    }

    /* Validate version */
    if (sb->version != EKKFS_VERSION) {
        fs_debug("EKKFS: Unsupported version: %lu\n", sb->version);
        return EKKFS_ERR_CORRUPT;
    }

    /* Validate CRC */
    uint32_t crc = ekkfs_crc32(sb, offsetof(ekkfs_superblock_t, crc32));
    if (crc != sb->crc32) {
        fs_debug("EKKFS: CRC mismatch: expected 0x%08lx, got 0x%08lx\n", sb->crc32, crc);
        return EKKFS_ERR_CORRUPT;
    }

    /* Store state */
    g_fs.partition_lba = partition_lba;
    memcpy(&g_fs.superblock, sb, sizeof(ekkfs_superblock_t));

    /* Calculate bitmap blocks */
    uint32_t data_blocks = g_fs.superblock.total_blocks - g_fs.superblock.data_start;
    g_fs.bitmap_blocks = (data_blocks + (8 * EKKFS_BLOCK_SIZE) - 1) / (8 * EKKFS_BLOCK_SIZE);

    if (g_fs.bitmap_blocks > MAX_BITMAP_BLOCKS) {
        fs_debug("EKKFS: Partition too large (bitmap needs %lu blocks)\n", g_fs.bitmap_blocks);
        return EKKFS_ERR_INVALID;
    }

    /* Load bitmap */
    if (bitmap_load() != EKKFS_OK) {
        fs_debug("EKKFS: Failed to load bitmap\n");
        return EKKFS_ERR_IO;
    }

    /* Recover journal (rollback incomplete transactions) */
    int journal_result = ekkfs_journal_recover();
    if (journal_result != EKKFS_OK) {
        fs_debug("EKKFS: Journal recovery failed: %d\n", journal_result);
        /* Continue anyway - journal will be reinitialized on next write */
    }

    /* Update mount count and time */
    g_fs.superblock.mount_count++;
    g_fs.superblock.mount_time = ekkfs_get_time_us();
    g_fs.superblock.crc32 = ekkfs_crc32(&g_fs.superblock, offsetof(ekkfs_superblock_t, crc32));

    /* Write updated superblock */
    memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
    memcpy(g_block_buf, &g_fs.superblock, sizeof(ekkfs_superblock_t));
    if (sd_write_block(partition_lba, g_block_buf) != SD_OK) {
        fs_debug("EKKFS: Failed to update superblock\n");
        return EKKFS_ERR_IO;
    }

    g_fs.mounted = 1;

    /* Initialize block cache */
    cache_init();

    fs_debug("EKKFS: Mounted successfully, %lu free blocks\n", g_fs.superblock.free_blocks);
    return EKKFS_OK;
}

int ekkfs_unmount(void)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    /* Flush and invalidate cache */
    cache_invalidate();

    /* Sync filesystem */
    ekkfs_sync();

    g_fs.mounted = 0;
    fs_debug("EKKFS: Unmounted\n");
    return EKKFS_OK;
}

int ekkfs_is_mounted(void)
{
    return g_fs.mounted;
}

int ekkfs_sync(void)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    /* Flush block cache */
    cache_flush();

    /* Save bitmap */
    if (bitmap_save() != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    /* Update and save superblock */
    g_fs.superblock.crc32 = ekkfs_crc32(&g_fs.superblock, offsetof(ekkfs_superblock_t, crc32));
    memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
    memcpy(g_block_buf, &g_fs.superblock, sizeof(ekkfs_superblock_t));
    if (sd_write_block(g_fs.partition_lba, g_block_buf) != SD_OK) {
        return EKKFS_ERR_IO;
    }

    return EKKFS_OK;
}

/* ============================================================================
 * File Operations
 * ============================================================================ */

int ekkfs_create(const char *name, uint32_t owner_id, uint32_t flags)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    size_t name_len = strlen(name);
    if (name_len > EKKFS_MAX_FILENAME) {
        return EKKFS_ERR_NAME_TOO_LONG;
    }

    /* Check if file already exists */
    if (inode_find_by_name(name) >= 0) {
        return EKKFS_ERR_EXISTS;
    }

    /* Allocate inode */
    int inode_num = inode_alloc();
    if (inode_num < 0) {
        return EKKFS_ERR_NO_INODES;
    }

    /* Initialize inode */
    ekkfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.flags = EKKFS_FLAG_USED | flags;
    inode.owner_id = owner_id;
    inode.size = 0;
    inode.created = ekkfs_get_time_us();
    inode.modified = inode.created;
    strncpy(inode.name, name, EKKFS_MAX_FILENAME);
    inode.name[EKKFS_MAX_FILENAME] = '\0';

    /* Write inode */
    if (inode_write(inode_num, &inode) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    fs_debug("EKKFS: Created file '%s' at inode %d\n", name, inode_num);
    return inode_num;
}

int ekkfs_delete(const char *name, uint32_t owner_id)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    int inode_num = inode_find_by_name(name);
    if (inode_num < 0) {
        return EKKFS_ERR_NOT_FOUND;
    }

    ekkfs_inode_t inode;
    if (inode_read(inode_num, &inode) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    /* Check permission (owner or system) */
    if (owner_id != 0 && inode.owner_id != owner_id) {
        return EKKFS_ERR_PERMISSION;
    }

    /* Don't delete system files unless requested by system */
    if ((inode.flags & EKKFS_FLAG_SYSTEM) && owner_id != 0) {
        return EKKFS_ERR_PERMISSION;
    }

    /* Free data blocks */
    for (int i = 0; i < EKKFS_DIRECT_BLOCKS; i++) {
        if (inode.blocks[i] != 0) {
            bitmap_free(inode.blocks[i]);
        }
    }

    /* Handle indirect block */
    if (inode.indirect != 0) {
        if (read_block(inode.indirect, g_block_buf) == SD_OK) {
            uint32_t *indirect_blocks = (uint32_t *)g_block_buf;
            for (uint32_t i = 0; i < EKKFS_BLOCK_SIZE / 4; i++) {
                if (indirect_blocks[i] != 0) {
                    bitmap_free(indirect_blocks[i]);
                }
            }
        }
        bitmap_free(inode.indirect);
    }

    /* Clear inode */
    memset(&inode, 0, sizeof(inode));
    if (inode_write(inode_num, &inode) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    fs_debug("EKKFS: Deleted file '%s'\n", name);
    return EKKFS_OK;
}

int ekkfs_open(ekkfs_file_t *file, const char *name)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    int inode_num = inode_find_by_name(name);
    if (inode_num < 0) {
        return EKKFS_ERR_NOT_FOUND;
    }

    file->inode_num = inode_num;
    file->position = 0;
    file->flags = 0;

    return EKKFS_OK;
}

int ekkfs_close(ekkfs_file_t *file)
{
    file->inode_num = 0;
    file->position = 0;
    file->flags = 0;
    return EKKFS_OK;
}

int ekkfs_read(ekkfs_file_t *file, void *buffer, uint32_t size)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    ekkfs_inode_t inode;
    if (inode_read(file->inode_num, &inode) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    if (!(inode.flags & EKKFS_FLAG_USED)) {
        return EKKFS_ERR_NOT_FOUND;
    }

    /* Calculate how much we can read */
    uint32_t remaining = (file->position < inode.size) ? (inode.size - file->position) : 0;
    if (size > remaining) {
        size = remaining;
    }

    if (size == 0) {
        return 0;
    }

    uint8_t *out = (uint8_t *)buffer;
    uint32_t bytes_read = 0;

    while (bytes_read < size) {
        uint32_t block_idx = file->position / EKKFS_BLOCK_SIZE;
        uint32_t block_offset = file->position % EKKFS_BLOCK_SIZE;
        uint32_t to_read = EKKFS_BLOCK_SIZE - block_offset;
        if (to_read > size - bytes_read) {
            to_read = size - bytes_read;
        }

        uint32_t block_num = 0;
        if (block_idx < EKKFS_DIRECT_BLOCKS) {
            block_num = inode.blocks[block_idx];
        } else if (inode.indirect != 0) {
            /* Read from indirect block */
            if (read_block(inode.indirect, g_block_buf) != SD_OK) {
                return EKKFS_ERR_IO;
            }
            uint32_t *indirect_blocks = (uint32_t *)g_block_buf;
            block_num = indirect_blocks[block_idx - EKKFS_DIRECT_BLOCKS];
        }

        if (block_num != 0) {
            if (read_block(block_num, g_block_buf) != SD_OK) {
                return EKKFS_ERR_IO;
            }
            memcpy(out + bytes_read, g_block_buf + block_offset, to_read);
        } else {
            /* Sparse block - fill with zeros */
            memset(out + bytes_read, 0, to_read);
        }

        bytes_read += to_read;
        file->position += to_read;
    }

    return (int)bytes_read;
}

int ekkfs_write(ekkfs_file_t *file, const void *buffer, uint32_t size, uint32_t owner_id)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    ekkfs_inode_t inode;
    if (inode_read(file->inode_num, &inode) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    if (!(inode.flags & EKKFS_FLAG_USED)) {
        return EKKFS_ERR_NOT_FOUND;
    }

    /* Check permission */
    if (owner_id != 0 && inode.owner_id != owner_id) {
        return EKKFS_ERR_PERMISSION;
    }

    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t bytes_written = 0;

    while (bytes_written < size) {
        uint32_t block_idx = file->position / EKKFS_BLOCK_SIZE;
        uint32_t block_offset = file->position % EKKFS_BLOCK_SIZE;
        uint32_t to_write = EKKFS_BLOCK_SIZE - block_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }

        uint32_t *block_ptr = NULL;
        uint32_t indirect_buf[EKKFS_BLOCK_SIZE / 4];

        if (block_idx < EKKFS_DIRECT_BLOCKS) {
            block_ptr = &inode.blocks[block_idx];
        } else {
            /* Need indirect block */
            if (inode.indirect == 0) {
                int new_indirect = bitmap_alloc();
                if (new_indirect < 0) {
                    /* Save what we've written so far */
                    inode.modified = ekkfs_get_time_us();
                    if (file->position > inode.size) {
                        inode.size = file->position;
                    }
                    inode_write(file->inode_num, &inode);
                    return (bytes_written > 0) ? (int)bytes_written : EKKFS_ERR_FULL;
                }
                inode.indirect = new_indirect;
                memset(indirect_buf, 0, sizeof(indirect_buf));
            } else {
                if (read_block(inode.indirect, indirect_buf) != SD_OK) {
                    return EKKFS_ERR_IO;
                }
            }
            block_ptr = &indirect_buf[block_idx - EKKFS_DIRECT_BLOCKS];
        }

        /* Allocate block if needed */
        if (*block_ptr == 0) {
            int new_block = bitmap_alloc();
            if (new_block < 0) {
                inode.modified = ekkfs_get_time_us();
                if (file->position > inode.size) {
                    inode.size = file->position;
                }
                inode_write(file->inode_num, &inode);
                return (bytes_written > 0) ? (int)bytes_written : EKKFS_ERR_FULL;
            }
            *block_ptr = new_block;

            /* Zero the new block */
            memset(g_block_buf, 0, EKKFS_BLOCK_SIZE);
        } else {
            /* Read existing block for partial write */
            if (read_block(*block_ptr, g_block_buf) != SD_OK) {
                return EKKFS_ERR_IO;
            }
        }

        /* Write data to block buffer */
        memcpy(g_block_buf + block_offset, in + bytes_written, to_write);

        /* Write block back */
        if (write_block(*block_ptr, g_block_buf) != SD_OK) {
            return EKKFS_ERR_IO;
        }

        /* Save indirect block if used */
        if (block_idx >= EKKFS_DIRECT_BLOCKS) {
            if (write_block(inode.indirect, indirect_buf) != SD_OK) {
                return EKKFS_ERR_IO;
            }
        }

        bytes_written += to_write;
        file->position += to_write;
    }

    /* Update inode */
    inode.modified = ekkfs_get_time_us();
    if (file->position > inode.size) {
        inode.size = file->position;
    }
    inode.crc32 = 0;  /* TODO: Calculate CRC of entire file */

    if (inode_write(file->inode_num, &inode) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    return (int)bytes_written;
}

int ekkfs_seek(ekkfs_file_t *file, uint32_t position)
{
    file->position = position;
    return EKKFS_OK;
}

int ekkfs_stat(const char *name, ekkfs_stat_t *stat)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    int inode_num = inode_find_by_name(name);
    if (inode_num < 0) {
        return EKKFS_ERR_NOT_FOUND;
    }

    ekkfs_inode_t inode;
    if (inode_read(inode_num, &inode) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    stat->inode_num = inode_num;
    stat->flags = inode.flags;
    stat->owner_id = inode.owner_id;
    stat->size = inode.size;
    stat->created = inode.created;
    stat->modified = inode.modified;
    strncpy(stat->name, inode.name, 16);

    return EKKFS_OK;
}

/* ============================================================================
 * Directory Operations
 * ============================================================================ */

int ekkfs_list(void (*callback)(uint32_t inode, const char *name, uint32_t size,
                                uint32_t owner, void *user_data), void *user_data)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    int count = 0;
    ekkfs_inode_t inode;

    for (uint32_t i = 0; i < g_fs.superblock.inode_count; i++) {
        if (inode_read(i, &inode) != EKKFS_OK) {
            continue;
        }
        if (inode.flags & EKKFS_FLAG_USED) {
            if (callback) {
                callback(i, inode.name, inode.size, inode.owner_id, user_data);
            }
            count++;
        }
    }

    return count;
}

int ekkfs_statfs(uint32_t *total_blocks, uint32_t *free_blocks,
                 uint32_t *total_inodes, uint32_t *used_inodes)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    if (total_blocks) *total_blocks = g_fs.superblock.total_blocks;
    if (free_blocks) *free_blocks = g_fs.superblock.free_blocks;
    if (total_inodes) *total_inodes = g_fs.superblock.inode_count;

    if (used_inodes) {
        uint32_t used = 0;
        ekkfs_inode_t inode;
        for (uint32_t i = 0; i < g_fs.superblock.inode_count; i++) {
            if (inode_read(i, &inode) == EKKFS_OK && (inode.flags & EKKFS_FLAG_USED)) {
                used++;
            }
        }
        *used_inodes = used;
    }

    return EKKFS_OK;
}

/* ============================================================================
 * Journal Operations
 * ============================================================================ */

/* Journal buffer */
static uint8_t g_journal_buf[EKKFS_BLOCK_SIZE] __attribute__((aligned(16)));

/**
 * @brief Read a journal block
 */
static int journal_read_block(uint32_t journal_block_idx, void *buffer)
{
    if (journal_block_idx >= EKKFS_JOURNAL_BLOCKS) {
        return EKKFS_ERR_INVALID;
    }
    return read_block(g_fs.superblock.journal_start + journal_block_idx, buffer);
}

/**
 * @brief Write a journal block
 */
static int journal_write_block(uint32_t journal_block_idx, const void *buffer)
{
    if (journal_block_idx >= EKKFS_JOURNAL_BLOCKS) {
        return EKKFS_ERR_INVALID;
    }
    return write_block(g_fs.superblock.journal_start + journal_block_idx, buffer);
}

/**
 * @brief Get pointer to journal entry in buffer
 *
 * Journal block layout:
 * - Block 0: Header (64 bytes) + 7 entries (7 * 32 = 224 bytes) = 288 bytes used
 * - Block 1-3: 16 entries each (16 * 32 = 512 bytes)
 *
 * Total entries: 7 + 16*3 = 55 entries
 */
static ekkfs_journal_entry_t *journal_get_entry_ptr(uint8_t *block_buf, uint32_t entry_in_block)
{
    return (ekkfs_journal_entry_t *)(block_buf + entry_in_block * sizeof(ekkfs_journal_entry_t));
}

/**
 * @brief Calculate total number of journal entries across all blocks
 */
static uint32_t journal_total_entries(void)
{
    /* Block 0 has header (64 bytes) + entries */
    /* 512 - 64 = 448 bytes = 14 entries in first block */
    /* Blocks 1-3 have 16 entries each */
    return 14 + (EKKFS_JOURNAL_BLOCKS - 1) * 16;
}

/**
 * @brief Convert linear entry index to block and offset
 */
static void journal_entry_location(uint32_t entry_idx, uint32_t *block_idx, uint32_t *entry_offset)
{
    if (entry_idx < 14) {
        /* First block (after header) */
        *block_idx = 0;
        *entry_offset = sizeof(ekkfs_journal_header_t) + entry_idx * sizeof(ekkfs_journal_entry_t);
    } else {
        /* Subsequent blocks */
        uint32_t remaining = entry_idx - 14;
        *block_idx = 1 + remaining / 16;
        *entry_offset = (remaining % 16) * sizeof(ekkfs_journal_entry_t);
    }
}

int ekkfs_journal_init(void)
{
    fs_debug("EKKFS: Initializing journal\n");

    /* Initialize header */
    ekkfs_journal_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = EKKFS_JOURNAL_MAGIC;
    header.head = 0;
    header.tail = 0;
    header.sequence = 1;
    header.tx_active = 0;
    header.tx_start_seq = 0;
    header.crc32 = ekkfs_crc32(&header, offsetof(ekkfs_journal_header_t, crc32));

    /* Write header block (block 0 of journal) */
    memset(g_journal_buf, 0, EKKFS_BLOCK_SIZE);
    memcpy(g_journal_buf, &header, sizeof(header));

    if (write_block(g_fs.superblock.journal_start, g_journal_buf) != SD_OK) {
        fs_debug("EKKFS: Failed to write journal header\n");
        return EKKFS_ERR_IO;
    }

    /* Clear remaining journal blocks */
    memset(g_journal_buf, 0, EKKFS_BLOCK_SIZE);
    for (uint32_t i = 1; i < EKKFS_JOURNAL_BLOCKS; i++) {
        if (write_block(g_fs.superblock.journal_start + i, g_journal_buf) != SD_OK) {
            fs_debug("EKKFS: Failed to clear journal block %lu\n", i);
            return EKKFS_ERR_IO;
        }
    }

    /* Initialize runtime state */
    memcpy(&g_fs.journal.header, &header, sizeof(header));
    g_fs.journal.current_tx_seq = 0;
    g_fs.journal.tx_active = 0;
    g_fs.journal.dirty = 0;

    fs_debug("EKKFS: Journal initialized\n");
    return EKKFS_OK;
}

int ekkfs_journal_recover(void)
{
    fs_debug("EKKFS: Recovering journal\n");

    /* Read journal header */
    if (read_block(g_fs.superblock.journal_start, g_journal_buf) != SD_OK) {
        fs_debug("EKKFS: Failed to read journal header\n");
        return EKKFS_ERR_IO;
    }

    ekkfs_journal_header_t *header = (ekkfs_journal_header_t *)g_journal_buf;

    /* Check magic */
    if (header->magic != EKKFS_JOURNAL_MAGIC) {
        fs_debug("EKKFS: Invalid journal magic, reinitializing\n");
        return ekkfs_journal_init();
    }

    /* Verify CRC */
    uint32_t crc = ekkfs_crc32(header, offsetof(ekkfs_journal_header_t, crc32));
    if (crc != header->crc32) {
        fs_debug("EKKFS: Journal header CRC mismatch, reinitializing\n");
        return ekkfs_journal_init();
    }

    /* Copy header to runtime state */
    memcpy(&g_fs.journal.header, header, sizeof(ekkfs_journal_header_t));
    g_fs.journal.tx_active = 0;
    g_fs.journal.dirty = 0;

    /* Check for incomplete transaction */
    if (header->tx_active) {
        fs_debug("EKKFS: Found incomplete transaction (seq=%lu), rolling back\n",
                 header->tx_start_seq);

        /* Scan journal entries from tail to head, looking for entries from incomplete tx */
        uint32_t total = journal_total_entries();
        uint32_t pos = header->tail;
        uint32_t rollback_count = 0;

        while (pos != header->head) {
            uint32_t block_idx, entry_offset;
            journal_entry_location(pos, &block_idx, &entry_offset);

            if (journal_read_block(block_idx, g_journal_buf) != EKKFS_OK) {
                fs_debug("EKKFS: Failed to read journal block during recovery\n");
                break;
            }

            ekkfs_journal_entry_t *entry = (ekkfs_journal_entry_t *)(g_journal_buf + entry_offset);

            /* Check if entry belongs to incomplete transaction */
            if (entry->sequence >= header->tx_start_seq &&
                entry->type != EKKFS_JOURNAL_COMMIT &&
                entry->type != EKKFS_JOURNAL_NOP) {

                /* Rollback: restore old value */
                switch (entry->type) {
                    case EKKFS_JOURNAL_ALLOC_BLOCK:
                        /* Undo block allocation - mark as free */
                        bitmap_set(entry->new_value, 0);
                        g_fs.superblock.free_blocks++;
                        rollback_count++;
                        break;

                    case EKKFS_JOURNAL_FREE_BLOCK:
                        /* Undo block free - mark as used */
                        bitmap_set(entry->old_value, 1);
                        g_fs.superblock.free_blocks--;
                        rollback_count++;
                        break;

                    default:
                        /* Other operations would need more complex rollback */
                        break;
                }
            }

            pos = (pos + 1) % total;
        }

        if (rollback_count > 0) {
            fs_debug("EKKFS: Rolled back %lu operations\n", rollback_count);
            bitmap_save();
        }

        /* Clear incomplete transaction flag */
        g_fs.journal.header.tx_active = 0;
        g_fs.journal.header.crc32 = ekkfs_crc32(&g_fs.journal.header,
                                                  offsetof(ekkfs_journal_header_t, crc32));

        /* Write updated header */
        memset(g_journal_buf, 0, EKKFS_BLOCK_SIZE);
        memcpy(g_journal_buf, &g_fs.journal.header, sizeof(ekkfs_journal_header_t));
        if (write_block(g_fs.superblock.journal_start, g_journal_buf) != SD_OK) {
            fs_debug("EKKFS: Failed to write journal header after recovery\n");
            return EKKFS_ERR_IO;
        }
    }

    /* Replay committed transactions */
    /* For simplicity, we just clear old entries - actual replay would restore
       committed changes if they weren't flushed to disk */
    fs_debug("EKKFS: Journal recovery complete (head=%lu, tail=%lu, seq=%lu)\n",
             g_fs.journal.header.head, g_fs.journal.header.tail,
             g_fs.journal.header.sequence);

    return EKKFS_OK;
}

int ekkfs_tx_begin(void)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    if (g_fs.journal.tx_active) {
        fs_debug("EKKFS: Transaction already in progress\n");
        return EKKFS_ERR_INVALID;
    }

    /* Start new transaction */
    g_fs.journal.current_tx_seq = g_fs.journal.header.sequence;
    g_fs.journal.tx_active = 1;
    g_fs.journal.header.tx_active = 1;
    g_fs.journal.header.tx_start_seq = g_fs.journal.current_tx_seq;
    g_fs.journal.dirty = 1;

    /* Update header on disk to mark transaction start */
    g_fs.journal.header.crc32 = ekkfs_crc32(&g_fs.journal.header,
                                             offsetof(ekkfs_journal_header_t, crc32));

    memset(g_journal_buf, 0, EKKFS_BLOCK_SIZE);
    memcpy(g_journal_buf, &g_fs.journal.header, sizeof(ekkfs_journal_header_t));

    /* Read existing entries in first journal block to preserve them */
    if (read_block(g_fs.superblock.journal_start, g_journal_buf) != SD_OK) {
        g_fs.journal.tx_active = 0;
        return EKKFS_ERR_IO;
    }

    /* Update header portion only */
    memcpy(g_journal_buf, &g_fs.journal.header, sizeof(ekkfs_journal_header_t));

    if (write_block(g_fs.superblock.journal_start, g_journal_buf) != SD_OK) {
        g_fs.journal.tx_active = 0;
        return EKKFS_ERR_IO;
    }

    fs_debug("EKKFS: Transaction %lu started\n", g_fs.journal.current_tx_seq);
    return (int)g_fs.journal.current_tx_seq;
}

int ekkfs_tx_commit(void)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    if (!g_fs.journal.tx_active) {
        fs_debug("EKKFS: No transaction to commit\n");
        return EKKFS_ERR_INVALID;
    }

    /* Log commit marker */
    int result = ekkfs_journal_log(EKKFS_JOURNAL_COMMIT, 0, 0, 0, 0);
    if (result != EKKFS_OK) {
        fs_debug("EKKFS: Failed to log commit marker\n");
        return result;
    }

    /* Update header - transaction complete */
    g_fs.journal.header.tx_active = 0;
    g_fs.journal.header.sequence++;
    g_fs.journal.header.crc32 = ekkfs_crc32(&g_fs.journal.header,
                                             offsetof(ekkfs_journal_header_t, crc32));

    /* Write header */
    if (read_block(g_fs.superblock.journal_start, g_journal_buf) != SD_OK) {
        return EKKFS_ERR_IO;
    }
    memcpy(g_journal_buf, &g_fs.journal.header, sizeof(ekkfs_journal_header_t));
    if (write_block(g_fs.superblock.journal_start, g_journal_buf) != SD_OK) {
        return EKKFS_ERR_IO;
    }

    /* Sync filesystem to ensure data is on disk */
    result = ekkfs_sync();

    g_fs.journal.tx_active = 0;
    g_fs.journal.dirty = 0;

    fs_debug("EKKFS: Transaction %lu committed\n", g_fs.journal.current_tx_seq);
    return EKKFS_OK;
}

int ekkfs_tx_abort(void)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    if (!g_fs.journal.tx_active) {
        fs_debug("EKKFS: No transaction to abort\n");
        return EKKFS_ERR_INVALID;
    }

    fs_debug("EKKFS: Aborting transaction %lu\n", g_fs.journal.current_tx_seq);

    /* Scan and rollback all entries from this transaction */
    uint32_t total = journal_total_entries();
    uint32_t pos = g_fs.journal.header.tail;
    uint32_t rollback_count = 0;

    while (pos != g_fs.journal.header.head) {
        uint32_t block_idx, entry_offset;
        journal_entry_location(pos, &block_idx, &entry_offset);

        if (journal_read_block(block_idx, g_journal_buf) != EKKFS_OK) {
            break;
        }

        ekkfs_journal_entry_t *entry = (ekkfs_journal_entry_t *)(g_journal_buf + entry_offset);

        if (entry->sequence >= g_fs.journal.header.tx_start_seq &&
            entry->type != EKKFS_JOURNAL_NOP) {

            /* Rollback based on entry type */
            switch (entry->type) {
                case EKKFS_JOURNAL_ALLOC_BLOCK:
                    bitmap_set(entry->new_value, 0);
                    g_fs.superblock.free_blocks++;
                    rollback_count++;
                    break;

                case EKKFS_JOURNAL_FREE_BLOCK:
                    bitmap_set(entry->old_value, 1);
                    g_fs.superblock.free_blocks--;
                    rollback_count++;
                    break;

                default:
                    break;
            }
        }

        pos = (pos + 1) % total;
    }

    /* Reset head to discard transaction entries */
    /* Note: In a real implementation, we'd need to reload data from disk */
    g_fs.journal.header.tx_active = 0;
    g_fs.journal.header.crc32 = ekkfs_crc32(&g_fs.journal.header,
                                             offsetof(ekkfs_journal_header_t, crc32));

    /* Write header */
    if (read_block(g_fs.superblock.journal_start, g_journal_buf) == SD_OK) {
        memcpy(g_journal_buf, &g_fs.journal.header, sizeof(ekkfs_journal_header_t));
        write_block(g_fs.superblock.journal_start, g_journal_buf);
    }

    /* Save bitmap with rollback changes */
    if (rollback_count > 0) {
        bitmap_save();
    }

    g_fs.journal.tx_active = 0;
    g_fs.journal.dirty = 0;

    fs_debug("EKKFS: Transaction aborted, rolled back %lu operations\n", rollback_count);
    return EKKFS_OK;
}

int ekkfs_journal_log(uint32_t type, uint32_t inode, uint32_t block,
                      uint32_t old_value, uint32_t new_value)
{
    if (!g_fs.mounted) {
        return EKKFS_ERR_NOT_MOUNTED;
    }

    /* Allow logging outside transactions for simple operations */
    uint32_t seq = g_fs.journal.tx_active ? g_fs.journal.current_tx_seq
                                           : g_fs.journal.header.sequence;

    /* Calculate entry position */
    uint32_t total = journal_total_entries();
    uint32_t head = g_fs.journal.header.head;
    uint32_t tail = g_fs.journal.header.tail;

    /* Check if journal is full */
    uint32_t next_head = (head + 1) % total;
    if (next_head == tail) {
        /* Journal full - advance tail (discard oldest entry) */
        tail = (tail + 1) % total;
        g_fs.journal.header.tail = tail;
    }

    /* Create entry */
    ekkfs_journal_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.sequence = seq;
    entry.type = type;
    entry.inode = inode;
    entry.block = block;
    entry.old_value = old_value;
    entry.new_value = new_value;
    entry.timestamp = (uint32_t)(ekkfs_get_time_us() / 1000000);  /* Seconds */
    entry.crc32 = ekkfs_crc32(&entry, offsetof(ekkfs_journal_entry_t, crc32));

    /* Find location and write */
    uint32_t block_idx, entry_offset;
    journal_entry_location(head, &block_idx, &entry_offset);

    if (journal_read_block(block_idx, g_journal_buf) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    memcpy(g_journal_buf + entry_offset, &entry, sizeof(entry));

    if (journal_write_block(block_idx, g_journal_buf) != EKKFS_OK) {
        return EKKFS_ERR_IO;
    }

    /* Update head */
    g_fs.journal.header.head = next_head;
    g_fs.journal.dirty = 1;

    /* Update header on disk if not in transaction (immediate durability) */
    if (!g_fs.journal.tx_active) {
        g_fs.journal.header.crc32 = ekkfs_crc32(&g_fs.journal.header,
                                                 offsetof(ekkfs_journal_header_t, crc32));

        if (journal_read_block(0, g_journal_buf) == EKKFS_OK) {
            memcpy(g_journal_buf, &g_fs.journal.header, sizeof(ekkfs_journal_header_t));
            journal_write_block(0, g_journal_buf);
        }
    }

    return EKKFS_OK;
}
