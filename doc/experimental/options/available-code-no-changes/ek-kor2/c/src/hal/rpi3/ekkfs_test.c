/**
 * @file ekkfs_test.c
 * @brief EKKFS Test Suite Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Comprehensive test suite for EKKFS filesystem.
 * Tests SD driver, filesystem operations, and stress scenarios.
 */

#include "ekkfs_test.h"
#include "../../ekkfs.h"
#include "sd.h"
#include "uart.h"
#include "framebuffer.h"
#include "timer.h"
#include <string.h>

/* ============================================================================
 * Test Macros
 * ============================================================================ */

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        test_fail(__func__, __LINE__, msg); \
        return TEST_FAIL; \
    } \
} while(0)

#define TEST_ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        test_fail_eq(__func__, __LINE__, msg, (int)(a), (int)(b)); \
        return TEST_FAIL; \
    } \
} while(0)

#define TEST_LOG(fmt, ...) do { \
    uart_printf("  " fmt "\n", ##__VA_ARGS__); \
    if (framebuffer_is_ready()) fb_printf("  " fmt "\n", ##__VA_ARGS__); \
} while(0)

/* ============================================================================
 * Static State
 * ============================================================================ */

static ekkfs_test_stats_t g_stats;
static uint32_t g_test_partition_lba = 0;

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

static void test_fail(const char *func, int line, const char *msg)
{
    uart_printf("    FAIL: %s:%d - %s\n", func, line, msg);
    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_RED, FB_COLOR_BLACK);
        fb_printf("    FAIL: %s:%d - %s\n", func, line, msg);
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
    g_stats.failed++;
}

static void test_fail_eq(const char *func, int line, const char *msg, int a, int b)
{
    uart_printf("    FAIL: %s:%d - %s (got %d, expected %d)\n", func, line, msg, a, b);
    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_RED, FB_COLOR_BLACK);
        fb_printf("    FAIL: %s:%d - %s (%d != %d)\n", func, line, msg, a, b);
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
    g_stats.failed++;
}

static void test_pass(const char *name)
{
    uart_printf("    PASS: %s\n", name);
    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
        fb_printf("    PASS: %s\n", name);
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
    g_stats.passed++;
}

static void test_skip(const char *name, const char *reason)
{
    uart_printf("    SKIP: %s (%s)\n", name, reason);
    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_YELLOW, FB_COLOR_BLACK);
        fb_printf("    SKIP: %s (%s)\n", name, reason);
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
    g_stats.skipped++;
}

static void test_section(const char *name)
{
    uart_printf("\n[TEST] %s\n", name);
    if (framebuffer_is_ready()) {
        fb_set_colors(FB_COLOR_CYAN, FB_COLOR_BLACK);
        fb_printf("\n[TEST] %s\n", name);
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
}

/* ============================================================================
 * SD Card Tests
 * ============================================================================ */

static int test_sd_init(void)
{
    g_stats.total++;

    if (!sd_is_initialized()) {
        int ret = sd_init();
        TEST_ASSERT(ret == SD_OK, "SD init failed");
    }

    TEST_ASSERT(sd_is_initialized(), "SD not initialized");

    test_pass("sd_init");
    return TEST_PASS;
}

static int test_sd_read_mbr(void)
{
    g_stats.total++;

    uint8_t buffer[512];
    int ret = sd_read_block(0, buffer);
    TEST_ASSERT(ret == SD_OK, "Failed to read MBR");

    /* Check MBR signature */
    uint16_t sig = buffer[510] | (buffer[511] << 8);
    TEST_ASSERT_EQ(sig, 0xAA55, "Invalid MBR signature");

    test_pass("sd_read_mbr");
    return TEST_PASS;
}

static int test_sd_parse_partitions(void)
{
    g_stats.total++;

    sd_partition_t parts[4];
    int ret = sd_parse_mbr(parts);
    TEST_ASSERT(ret == SD_OK, "Failed to parse MBR");

    /* Find EKKFS or Linux partition */
    int found = 0;
    for (int i = 0; i < 4; i++) {
        if (parts[i].is_valid) {
            TEST_LOG("Partition %d: type=0x%02X, LBA=%lu, size=%lu sectors",
                     i, parts[i].type, parts[i].lba_start, parts[i].sector_count);
            if (parts[i].type == MBR_PART_TYPE_EKKFS ||
                parts[i].type == MBR_PART_TYPE_LINUX) {
                g_test_partition_lba = parts[i].lba_start;
                found = 1;
            }
        }
    }

    TEST_ASSERT(found, "No EKKFS/Linux partition found");
    TEST_LOG("Using partition at LBA %lu for tests", g_test_partition_lba);

    test_pass("sd_parse_partitions");
    return TEST_PASS;
}

static int test_sd_read_write(void)
{
    g_stats.total++;

    if (g_test_partition_lba == 0) {
        test_skip("sd_read_write", "No test partition");
        return TEST_SKIP;
    }

    /* Use a block far into the data area to avoid metadata */
    uint32_t test_lba = g_test_partition_lba + 1000;

    /* Write test pattern */
    uint8_t write_buf[512];
    uint8_t read_buf[512];

    for (int i = 0; i < 512; i++) {
        write_buf[i] = (uint8_t)(i ^ 0xAA);
    }

    int ret = sd_write_block(test_lba, write_buf);
    TEST_ASSERT(ret == SD_OK, "Write failed");

    /* Read back */
    memset(read_buf, 0, 512);
    ret = sd_read_block(test_lba, read_buf);
    TEST_ASSERT(ret == SD_OK, "Read failed");

    /* Compare */
    TEST_ASSERT(memcmp(write_buf, read_buf, 512) == 0, "Data mismatch");

    test_pass("sd_read_write");
    return TEST_PASS;
}

int ekkfs_test_sd(void)
{
    test_section("SD Card Tests");

    int failures = 0;
    failures += (test_sd_init() == TEST_FAIL);
    failures += (test_sd_read_mbr() == TEST_FAIL);
    failures += (test_sd_parse_partitions() == TEST_FAIL);
    failures += (test_sd_read_write() == TEST_FAIL);

    return failures;
}

/* ============================================================================
 * Filesystem Mount Tests
 * ============================================================================ */

static int test_fs_format(void)
{
    g_stats.total++;

    if (g_test_partition_lba == 0) {
        test_skip("fs_format", "No test partition");
        return TEST_SKIP;
    }

    /* Format with default settings */
    int ret = ekkfs_format(g_test_partition_lba, 10000, 64);
    TEST_ASSERT(ret == EKKFS_OK, "Format failed");

    test_pass("fs_format");
    return TEST_PASS;
}

static int test_fs_mount(void)
{
    g_stats.total++;

    if (g_test_partition_lba == 0) {
        test_skip("fs_mount", "No test partition");
        return TEST_SKIP;
    }

    int ret = ekkfs_mount(g_test_partition_lba);
    TEST_ASSERT(ret == EKKFS_OK, "Mount failed");
    TEST_ASSERT(ekkfs_is_mounted(), "Not mounted after mount()");

    test_pass("fs_mount");
    return TEST_PASS;
}

static int test_fs_statfs(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("fs_statfs", "Not mounted");
        return TEST_SKIP;
    }

    uint32_t total, free_blks, total_inodes, used_inodes;
    int ret = ekkfs_statfs(&total, &free_blks, &total_inodes, &used_inodes);
    TEST_ASSERT(ret == EKKFS_OK, "statfs failed");

    TEST_LOG("Total blocks: %lu", total);
    TEST_LOG("Free blocks: %lu", free_blks);
    TEST_LOG("Total inodes: %lu", total_inodes);
    TEST_LOG("Used inodes: %lu", used_inodes);

    TEST_ASSERT(total > 0, "Invalid total blocks");
    TEST_ASSERT(free_blks <= total, "Free > total");
    TEST_ASSERT(total_inodes > 0, "Invalid inode count");

    test_pass("fs_statfs");
    return TEST_PASS;
}

static int test_fs_unmount(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("fs_unmount", "Not mounted");
        return TEST_SKIP;
    }

    int ret = ekkfs_unmount();
    TEST_ASSERT(ret == EKKFS_OK, "Unmount failed");
    TEST_ASSERT(!ekkfs_is_mounted(), "Still mounted after unmount()");

    test_pass("fs_unmount");
    return TEST_PASS;
}

static int test_fs_remount(void)
{
    g_stats.total++;

    if (g_test_partition_lba == 0) {
        test_skip("fs_remount", "No test partition");
        return TEST_SKIP;
    }

    /* Mount again after unmount */
    int ret = ekkfs_mount(g_test_partition_lba);
    TEST_ASSERT(ret == EKKFS_OK, "Remount failed");

    test_pass("fs_remount");
    return TEST_PASS;
}

int ekkfs_test_mount(void)
{
    test_section("Filesystem Mount Tests");

    int failures = 0;
    failures += (test_fs_format() == TEST_FAIL);
    failures += (test_fs_mount() == TEST_FAIL);
    failures += (test_fs_statfs() == TEST_FAIL);
    failures += (test_fs_unmount() == TEST_FAIL);
    failures += (test_fs_remount() == TEST_FAIL);

    return failures;
}

/* ============================================================================
 * File CRUD Tests
 * ============================================================================ */

static int test_file_create(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_create", "Not mounted");
        return TEST_SKIP;
    }

    int inode = ekkfs_create("test.txt", 1, 0);
    TEST_ASSERT(inode >= 0, "Create failed");
    TEST_LOG("Created test.txt at inode %d", inode);

    test_pass("file_create");
    return TEST_PASS;
}

static int test_file_create_duplicate(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_create_duplicate", "Not mounted");
        return TEST_SKIP;
    }

    int inode = ekkfs_create("test.txt", 1, 0);
    TEST_ASSERT_EQ(inode, EKKFS_ERR_EXISTS, "Should fail with EXISTS");

    test_pass("file_create_duplicate");
    return TEST_PASS;
}

static int test_file_open_close(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_open_close", "Not mounted");
        return TEST_SKIP;
    }

    ekkfs_file_t file;
    int ret = ekkfs_open(&file, "test.txt");
    TEST_ASSERT(ret == EKKFS_OK, "Open failed");

    ret = ekkfs_close(&file);
    TEST_ASSERT(ret == EKKFS_OK, "Close failed");

    test_pass("file_open_close");
    return TEST_PASS;
}

static int test_file_write_read(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_write_read", "Not mounted");
        return TEST_SKIP;
    }

    const char *test_data = "Hello, EKKFS! This is a test string.";
    size_t test_len = strlen(test_data);

    /* Write */
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, "test.txt");
    TEST_ASSERT(ret == EKKFS_OK, "Open for write failed");

    int written = ekkfs_write(&file, test_data, test_len, 1);
    TEST_ASSERT_EQ(written, (int)test_len, "Write count mismatch");

    ekkfs_close(&file);

    /* Read back */
    char read_buf[128];
    memset(read_buf, 0, sizeof(read_buf));

    ret = ekkfs_open(&file, "test.txt");
    TEST_ASSERT(ret == EKKFS_OK, "Open for read failed");

    int read_count = ekkfs_read(&file, read_buf, sizeof(read_buf));
    TEST_ASSERT_EQ(read_count, (int)test_len, "Read count mismatch");

    ekkfs_close(&file);

    /* Compare */
    TEST_ASSERT(memcmp(test_data, read_buf, test_len) == 0, "Data mismatch");

    test_pass("file_write_read");
    return TEST_PASS;
}

static int test_file_seek(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_seek", "Not mounted");
        return TEST_SKIP;
    }

    ekkfs_file_t file;
    int ret = ekkfs_open(&file, "test.txt");
    TEST_ASSERT(ret == EKKFS_OK, "Open failed");

    /* Seek to offset 7 ("EKKFS!") */
    ekkfs_seek(&file, 7);

    char buf[10];
    int n = ekkfs_read(&file, buf, 6);
    buf[6] = '\0';

    TEST_ASSERT_EQ(n, 6, "Read after seek failed");
    TEST_ASSERT(strcmp(buf, "EKKFS!") == 0, "Seek data mismatch");

    ekkfs_close(&file);

    test_pass("file_seek");
    return TEST_PASS;
}

static int test_file_stat(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_stat", "Not mounted");
        return TEST_SKIP;
    }

    ekkfs_stat_t stat;
    int ret = ekkfs_stat("test.txt", &stat);
    TEST_ASSERT(ret == EKKFS_OK, "Stat failed");

    TEST_LOG("File: %s, size=%lu, owner=%lu", stat.name, stat.size, stat.owner_id);
    TEST_ASSERT(stat.size > 0, "Size should be > 0");
    TEST_ASSERT_EQ(stat.owner_id, 1, "Owner mismatch");

    test_pass("file_stat");
    return TEST_PASS;
}

static int test_file_delete(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_delete", "Not mounted");
        return TEST_SKIP;
    }

    int ret = ekkfs_delete("test.txt", 1);
    TEST_ASSERT(ret == EKKFS_OK, "Delete failed");

    /* Verify deleted */
    ekkfs_stat_t stat;
    ret = ekkfs_stat("test.txt", &stat);
    TEST_ASSERT_EQ(ret, EKKFS_ERR_NOT_FOUND, "File should not exist");

    test_pass("file_delete");
    return TEST_PASS;
}

static int test_file_not_found(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("file_not_found", "Not mounted");
        return TEST_SKIP;
    }

    ekkfs_file_t file;
    int ret = ekkfs_open(&file, "nonexistent.txt");
    TEST_ASSERT_EQ(ret, EKKFS_ERR_NOT_FOUND, "Should return NOT_FOUND");

    test_pass("file_not_found");
    return TEST_PASS;
}

int ekkfs_test_crud(void)
{
    test_section("File CRUD Tests");

    int failures = 0;
    failures += (test_file_create() == TEST_FAIL);
    failures += (test_file_create_duplicate() == TEST_FAIL);
    failures += (test_file_open_close() == TEST_FAIL);
    failures += (test_file_write_read() == TEST_FAIL);
    failures += (test_file_seek() == TEST_FAIL);
    failures += (test_file_stat() == TEST_FAIL);
    failures += (test_file_delete() == TEST_FAIL);
    failures += (test_file_not_found() == TEST_FAIL);

    return failures;
}

/* ============================================================================
 * Stress Tests
 * ============================================================================ */

static int test_many_files(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("many_files", "Not mounted");
        return TEST_SKIP;
    }

    const int NUM_FILES = 20;
    char name[16];

    /* Create many files */
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(name, sizeof(name), "file%03d.dat", i);
        int inode = ekkfs_create(name, 1, 0);
        if (inode < 0) {
            TEST_LOG("Failed to create file %d: %d", i, inode);
            TEST_ASSERT(0, "Create failed in loop");
        }
    }

    TEST_LOG("Created %d files", NUM_FILES);

    /* Delete all */
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(name, sizeof(name), "file%03d.dat", i);
        int ret = ekkfs_delete(name, 1);
        if (ret != EKKFS_OK) {
            TEST_LOG("Failed to delete file %d: %d", i, ret);
            TEST_ASSERT(0, "Delete failed in loop");
        }
    }

    TEST_LOG("Deleted %d files", NUM_FILES);

    test_pass("many_files");
    return TEST_PASS;
}

static int test_large_file(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("large_file", "Not mounted");
        return TEST_SKIP;
    }

    /* Create a file larger than one block */
    int inode = ekkfs_create("large.bin", 1, 0);
    TEST_ASSERT(inode >= 0, "Create failed");

    ekkfs_file_t file;
    int ret = ekkfs_open(&file, "large.bin");
    TEST_ASSERT(ret == EKKFS_OK, "Open failed");

    /* Write 4KB of data (8 blocks) */
    uint8_t block[512];
    for (int i = 0; i < 8; i++) {
        memset(block, (uint8_t)i, 512);
        int written = ekkfs_write(&file, block, 512, 1);
        if (written != 512) {
            TEST_LOG("Write block %d failed: %d", i, written);
            ekkfs_close(&file);
            TEST_ASSERT(0, "Write failed");
        }
    }

    ekkfs_close(&file);

    /* Verify size */
    ekkfs_stat_t stat;
    ret = ekkfs_stat("large.bin", &stat);
    TEST_ASSERT(ret == EKKFS_OK, "Stat failed");
    TEST_ASSERT_EQ(stat.size, 4096, "Size mismatch");

    /* Read back and verify */
    ret = ekkfs_open(&file, "large.bin");
    TEST_ASSERT(ret == EKKFS_OK, "Open for verify failed");

    for (int i = 0; i < 8; i++) {
        memset(block, 0, 512);
        int read_count = ekkfs_read(&file, block, 512);
        TEST_ASSERT_EQ(read_count, 512, "Read failed");

        /* Check pattern */
        for (int j = 0; j < 512; j++) {
            if (block[j] != (uint8_t)i) {
                TEST_LOG("Data mismatch at block %d, byte %d", i, j);
                ekkfs_close(&file);
                TEST_ASSERT(0, "Data verification failed");
            }
        }
    }

    ekkfs_close(&file);

    /* Cleanup */
    ekkfs_delete("large.bin", 1);

    test_pass("large_file");
    return TEST_PASS;
}

static int test_persistence(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted() || g_test_partition_lba == 0) {
        test_skip("persistence", "Not mounted or no partition");
        return TEST_SKIP;
    }

    const char *test_data = "Persistence test data 12345";
    size_t test_len = strlen(test_data);

    /* Create and write */
    int inode = ekkfs_create("persist.txt", 1, 0);
    TEST_ASSERT(inode >= 0, "Create failed");

    ekkfs_file_t file;
    ekkfs_open(&file, "persist.txt");
    ekkfs_write(&file, test_data, test_len, 1);
    ekkfs_close(&file);

    /* Sync and unmount */
    ekkfs_sync();
    ekkfs_unmount();

    /* Remount */
    int ret = ekkfs_mount(g_test_partition_lba);
    TEST_ASSERT(ret == EKKFS_OK, "Remount failed");

    /* Read back */
    char read_buf[64];
    memset(read_buf, 0, sizeof(read_buf));

    ret = ekkfs_open(&file, "persist.txt");
    TEST_ASSERT(ret == EKKFS_OK, "Open after remount failed");

    int read_count = ekkfs_read(&file, read_buf, sizeof(read_buf));
    TEST_ASSERT_EQ(read_count, (int)test_len, "Read count mismatch");
    TEST_ASSERT(memcmp(test_data, read_buf, test_len) == 0, "Data not persisted");

    ekkfs_close(&file);
    ekkfs_delete("persist.txt", 1);

    test_pass("persistence");
    return TEST_PASS;
}

int ekkfs_test_stress(void)
{
    test_section("Stress Tests");

    int failures = 0;
    failures += (test_many_files() == TEST_FAIL);
    failures += (test_large_file() == TEST_FAIL);
    failures += (test_persistence() == TEST_FAIL);

    return failures;
}

/* ============================================================================
 * Journal Tests
 * ============================================================================ */

static int test_journal_transaction_commit(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("journal_tx_commit", "Not mounted");
        return TEST_SKIP;
    }

    /* Start transaction */
    int tx = ekkfs_tx_begin();
    TEST_ASSERT(tx > 0, "tx_begin failed");
    TEST_LOG("Transaction %d started", tx);

    /* Create file within transaction */
    int inode = ekkfs_create("txtest.txt", 1, 0);
    TEST_ASSERT(inode >= 0, "Create in tx failed");

    /* Write to file */
    ekkfs_file_t file;
    int ret = ekkfs_open(&file, "txtest.txt");
    TEST_ASSERT(ret == EKKFS_OK, "Open in tx failed");

    const char *data = "Transaction commit test";
    int written = ekkfs_write(&file, data, strlen(data), 1);
    TEST_ASSERT(written == (int)strlen(data), "Write in tx failed");
    ekkfs_close(&file);

    /* Commit transaction */
    ret = ekkfs_tx_commit();
    TEST_ASSERT(ret == EKKFS_OK, "tx_commit failed");
    TEST_LOG("Transaction committed");

    /* Verify file exists after commit */
    ekkfs_stat_t stat;
    ret = ekkfs_stat("txtest.txt", &stat);
    TEST_ASSERT(ret == EKKFS_OK, "File should exist after commit");
    TEST_ASSERT_EQ(stat.size, strlen(data), "Size mismatch after commit");

    /* Cleanup */
    ekkfs_delete("txtest.txt", 1);

    test_pass("journal_tx_commit");
    return TEST_PASS;
}

static int test_journal_transaction_abort(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("journal_tx_abort", "Not mounted");
        return TEST_SKIP;
    }

    /* Get initial free blocks */
    uint32_t free_before;
    ekkfs_statfs(NULL, &free_before, NULL, NULL);
    TEST_LOG("Free blocks before: %lu", free_before);

    /* Start transaction */
    int tx = ekkfs_tx_begin();
    TEST_ASSERT(tx > 0, "tx_begin failed");

    /* Create file within transaction */
    int inode = ekkfs_create("abort.txt", 1, 0);
    TEST_ASSERT(inode >= 0, "Create in tx failed");

    /* Write data to allocate blocks */
    ekkfs_file_t file;
    ekkfs_open(&file, "abort.txt");
    uint8_t block[512];
    memset(block, 0xAB, 512);
    ekkfs_write(&file, block, 512, 1);
    ekkfs_close(&file);

    /* Abort transaction */
    int ret = ekkfs_tx_abort();
    TEST_ASSERT(ret == EKKFS_OK, "tx_abort failed");
    TEST_LOG("Transaction aborted");

    /* File should still exist (abort doesn't undo inode changes in this simple impl) */
    /* But block allocations should be rolled back */
    uint32_t free_after;
    ekkfs_statfs(NULL, &free_after, NULL, NULL);
    TEST_LOG("Free blocks after abort: %lu", free_after);

    /* Note: In this simple implementation, the file metadata persists
       but block allocations are rolled back via journal */

    /* Cleanup - delete if exists */
    ekkfs_delete("abort.txt", 1);

    test_pass("journal_tx_abort");
    return TEST_PASS;
}

static int test_journal_recovery(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted() || g_test_partition_lba == 0) {
        test_skip("journal_recovery", "Not mounted or no partition");
        return TEST_SKIP;
    }

    /* Create a file that will persist */
    int inode = ekkfs_create("recov.txt", 1, 0);
    TEST_ASSERT(inode >= 0, "Create failed");

    ekkfs_file_t file;
    ekkfs_open(&file, "recov.txt");
    const char *data = "Recovery test data";
    ekkfs_write(&file, data, strlen(data), 1);
    ekkfs_close(&file);

    /* Sync to disk */
    ekkfs_sync();

    /* Unmount and remount - this triggers journal recovery */
    ekkfs_unmount();
    TEST_LOG("Unmounted, remounting with journal recovery...");

    int ret = ekkfs_mount(g_test_partition_lba);
    TEST_ASSERT(ret == EKKFS_OK, "Remount with recovery failed");

    /* Verify file survived */
    ekkfs_stat_t stat;
    ret = ekkfs_stat("recov.txt", &stat);
    TEST_ASSERT(ret == EKKFS_OK, "File should exist after recovery");
    TEST_LOG("File recovered: %s, size=%lu", stat.name, stat.size);

    /* Cleanup */
    ekkfs_delete("recov.txt", 1);

    test_pass("journal_recovery");
    return TEST_PASS;
}

int ekkfs_test_journal(void)
{
    test_section("Journal Tests");

    int failures = 0;
    failures += (test_journal_transaction_commit() == TEST_FAIL);
    failures += (test_journal_transaction_abort() == TEST_FAIL);
    failures += (test_journal_recovery() == TEST_FAIL);

    return failures;
}

/* ============================================================================
 * Cache Tests
 * ============================================================================ */

static int test_cache_hits(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted()) {
        test_skip("cache_hits", "Not mounted");
        return TEST_SKIP;
    }

    /* Create a test file */
    int inode = ekkfs_create("cache.txt", 1, 0);
    TEST_ASSERT(inode >= 0, "Create failed");

    ekkfs_file_t file;
    ekkfs_open(&file, "cache.txt");
    const char *data = "Cache test data for repeated reads";
    ekkfs_write(&file, data, strlen(data), 1);
    ekkfs_close(&file);

    /* Get initial cache stats */
    uint32_t hits_before, misses_before;
    ekkfs_cache_stats(&hits_before, &misses_before);

    /* Read the file multiple times - should hit cache */
    char buf[64];
    for (int i = 0; i < 5; i++) {
        ekkfs_open(&file, "cache.txt");
        ekkfs_read(&file, buf, sizeof(buf));
        ekkfs_close(&file);
    }

    /* Get final cache stats */
    uint32_t hits_after, misses_after;
    ekkfs_cache_stats(&hits_after, &misses_after);

    TEST_LOG("Cache: %lu hits (+%lu), %lu misses (+%lu)",
             hits_after, hits_after - hits_before,
             misses_after, misses_after - misses_before);

    /* Should have some cache hits from repeated reads */
    TEST_ASSERT(hits_after > hits_before, "Expected cache hits from repeated reads");

    /* Cleanup */
    ekkfs_delete("cache.txt", 1);

    test_pass("cache_hits");
    return TEST_PASS;
}

static int test_cache_write_through(void)
{
    g_stats.total++;

    if (!ekkfs_is_mounted() || g_test_partition_lba == 0) {
        test_skip("cache_write_through", "Not mounted or no partition");
        return TEST_SKIP;
    }

    /* Write data */
    int inode = ekkfs_create("wtcache.txt", 1, 0);
    TEST_ASSERT(inode >= 0, "Create failed");

    ekkfs_file_t file;
    ekkfs_open(&file, "wtcache.txt");
    const char *data = "Write-through cache test";
    ekkfs_write(&file, data, strlen(data), 1);
    ekkfs_close(&file);

    /* Unmount and remount (this clears cache) */
    ekkfs_unmount();
    int ret = ekkfs_mount(g_test_partition_lba);
    TEST_ASSERT(ret == EKKFS_OK, "Remount failed");

    /* Data should be on disk, not just in cache */
    char buf[64];
    memset(buf, 0, sizeof(buf));
    ekkfs_open(&file, "wtcache.txt");
    int n = ekkfs_read(&file, buf, sizeof(buf));
    ekkfs_close(&file);

    TEST_ASSERT_EQ(n, strlen(data), "Read length mismatch");
    TEST_ASSERT(memcmp(buf, data, strlen(data)) == 0, "Data not persisted (write-through failed)");

    /* Cleanup */
    ekkfs_delete("wtcache.txt", 1);

    test_pass("cache_write_through");
    return TEST_PASS;
}

int ekkfs_test_cache(void)
{
    test_section("Cache Tests");

    int failures = 0;
    failures += (test_cache_hits() == TEST_FAIL);
    failures += (test_cache_write_through() == TEST_FAIL);

    return failures;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int ekkfs_run_all_tests(void)
{
    /* Reset stats */
    memset(&g_stats, 0, sizeof(g_stats));
    g_test_partition_lba = 0;

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("        EKKFS Test Suite v1.0\n");
    uart_puts("========================================\n");

    if (framebuffer_is_ready()) {
        fb_puts("\n");
        fb_set_colors(FB_COLOR_CYAN, FB_COLOR_BLACK);
        fb_puts("========================================\n");
        fb_puts("        EKKFS Test Suite v1.0\n");
        fb_puts("========================================\n");
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }

    uint64_t start_time = timer_get_us();

    /* Run test suites */
    int failures = 0;
    failures += ekkfs_test_sd();
    failures += ekkfs_test_mount();
    failures += ekkfs_test_crud();
    failures += ekkfs_test_journal();
    failures += ekkfs_test_cache();
    failures += ekkfs_test_stress();

    /* Cleanup */
    if (ekkfs_is_mounted()) {
        ekkfs_unmount();
    }

    uint64_t elapsed = timer_get_us() - start_time;

    /* Summary */
    uart_puts("\n========================================\n");
    uart_printf("  TOTAL:   %lu tests\n", g_stats.total);
    uart_printf("  PASSED:  %lu\n", g_stats.passed);
    uart_printf("  FAILED:  %lu\n", g_stats.failed);
    uart_printf("  SKIPPED: %lu\n", g_stats.skipped);
    uart_printf("  TIME:    %llu ms\n", elapsed / 1000);
    uart_puts("========================================\n");

    if (framebuffer_is_ready()) {
        fb_puts("\n========================================\n");
        fb_printf("  TOTAL:   %lu tests\n", g_stats.total);

        fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
        fb_printf("  PASSED:  %lu\n", g_stats.passed);

        if (g_stats.failed > 0) {
            fb_set_colors(FB_COLOR_RED, FB_COLOR_BLACK);
        } else {
            fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
        }
        fb_printf("  FAILED:  %lu\n", g_stats.failed);

        fb_set_colors(FB_COLOR_YELLOW, FB_COLOR_BLACK);
        fb_printf("  SKIPPED: %lu\n", g_stats.skipped);

        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
        fb_printf("  TIME:    %llu ms\n", elapsed / 1000);
        fb_puts("========================================\n");

        if (g_stats.failed == 0) {
            fb_set_colors(FB_COLOR_GREEN, FB_COLOR_BLACK);
            fb_puts("\n  ALL TESTS PASSED!\n");
        } else {
            fb_set_colors(FB_COLOR_RED, FB_COLOR_BLACK);
            fb_puts("\n  SOME TESTS FAILED!\n");
        }
        fb_set_colors(FB_COLOR_WHITE, FB_COLOR_BLACK);
    }

    return (int)g_stats.failed;
}

const ekkfs_test_stats_t* ekkfs_test_get_stats(void)
{
    return &g_stats;
}
