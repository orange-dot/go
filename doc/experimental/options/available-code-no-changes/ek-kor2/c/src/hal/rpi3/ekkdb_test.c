/**
 * @file ekkdb_test.c
 * @brief EKKDB Unit Tests
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Unit tests for the database module. Run on RPi3 bare-metal.
 */

#include "../../ekkdb.h"
#include "../../ekkfs.h"
#include "db_server.h"
#include "uart.h"
#include <string.h>

/* ============================================================================
 * Test Framework
 * ============================================================================ */

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        uart_printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        uart_printf("  FAIL: %s: expected %d, got %d (%s:%d)\n", msg, (int)(b), (int)(a), __FILE__, __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS() do { \
    g_tests_passed++; \
    uart_puts("  PASS\n"); \
} while(0)

/* ============================================================================
 * Key-Value Tests
 * ============================================================================ */

static void test_kv_basic(void)
{
    uart_puts("test_kv_basic: ");

    /* Open KV store */
    int handle = ekkdb_kv_server_open("test", 0);
    TEST_ASSERT(handle >= 0, "KV open failed");

    /* Put a value */
    int ret = ekkdb_kv_server_put(handle, "key1", "value1", 6);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "KV put failed");

    /* Get the value back */
    char buf[16];
    uint32_t len = sizeof(buf);
    ret = ekkdb_kv_server_get(handle, "key1", buf, &len);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "KV get failed");
    TEST_ASSERT_EQ(len, 6, "KV get wrong length");
    TEST_ASSERT(memcmp(buf, "value1", 6) == 0, "KV get wrong value");

    /* Update the value */
    ret = ekkdb_kv_server_put(handle, "key1", "updated", 7);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "KV put update failed");

    len = sizeof(buf);
    ret = ekkdb_kv_server_get(handle, "key1", buf, &len);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "KV get after update failed");
    TEST_ASSERT_EQ(len, 7, "KV get wrong length after update");
    TEST_ASSERT(memcmp(buf, "updated", 7) == 0, "KV get wrong value after update");

    /* Delete the key */
    ret = ekkdb_kv_server_delete(handle, "key1");
    TEST_ASSERT_EQ(ret, EKKDB_OK, "KV delete failed");

    /* Verify key is gone */
    len = sizeof(buf);
    ret = ekkdb_kv_server_get(handle, "key1", buf, &len);
    TEST_ASSERT_EQ(ret, EKKDB_ERR_NOT_FOUND, "KV get after delete should fail");

    /* Close */
    ret = ekkdb_kv_server_close(handle);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "KV close failed");

    TEST_PASS();
}

static void test_kv_multiple_keys(void)
{
    uart_puts("test_kv_multiple_keys: ");

    int handle = ekkdb_kv_server_open("multi", 0);
    TEST_ASSERT(handle >= 0, "KV open failed");

    /* Add multiple keys */
    for (int i = 0; i < 10; i++) {
        char key[16], val[16];
        int klen = 0, vlen = 0;

        key[klen++] = 'k';
        key[klen++] = 'e';
        key[klen++] = 'y';
        key[klen++] = '0' + i;
        key[klen] = '\0';

        val[vlen++] = 'v';
        val[vlen++] = 'a';
        val[vlen++] = 'l';
        val[vlen++] = '0' + i;
        val[vlen] = '\0';

        int ret = ekkdb_kv_server_put(handle, key, val, vlen);
        TEST_ASSERT_EQ(ret, EKKDB_OK, "KV put multiple failed");
    }

    /* Verify count */
    int count = ekkdb_kv_server_count(handle);
    TEST_ASSERT_EQ(count, 10, "KV count wrong");

    /* Read back all keys */
    for (int i = 0; i < 10; i++) {
        char key[16], expected[16], buf[16];
        int klen = 0, elen = 0;

        key[klen++] = 'k';
        key[klen++] = 'e';
        key[klen++] = 'y';
        key[klen++] = '0' + i;
        key[klen] = '\0';

        expected[elen++] = 'v';
        expected[elen++] = 'a';
        expected[elen++] = 'l';
        expected[elen++] = '0' + i;
        expected[elen] = '\0';

        uint32_t len = sizeof(buf);
        int ret = ekkdb_kv_server_get(handle, key, buf, &len);
        TEST_ASSERT_EQ(ret, EKKDB_OK, "KV get multiple failed");
        TEST_ASSERT(memcmp(buf, expected, elen) == 0, "KV get multiple wrong value");
    }

    ekkdb_kv_server_close(handle);
    TEST_PASS();
}

/* ============================================================================
 * Time-Series Tests
 * ============================================================================ */

static void test_ts_basic(void)
{
    uart_puts("test_ts_basic: ");

    /* Open TS store */
    int handle = ekkdb_ts_server_open(42, "power", 0);
    TEST_ASSERT(handle >= 0, "TS open failed");

    /* Append some records */
    for (int i = 0; i < 10; i++) {
        ekkdb_ts_record_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp = 1000000 + i * 1000;  /* 1ms apart */
        rec.voltage_mv = 900000 + i * 100;
        rec.current_ma = 3000 + i * 10;
        rec.temp_mc = 25000 + i * 100;
        rec.power_mw = rec.voltage_mv * rec.current_ma / 1000000;
        rec.module_id = 42;

        int ret = ekkdb_ts_server_append(handle, &rec);
        TEST_ASSERT_EQ(ret, EKKDB_OK, "TS append failed");
    }

    /* Verify count */
    int count = ekkdb_ts_server_count(handle);
    TEST_ASSERT_EQ(count, 10, "TS count wrong");

    /* Query all records */
    uint32_t iter_handle, total_count;
    int ret = ekkdb_ts_server_query(handle, 0, 0, &iter_handle, &total_count);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "TS query failed");
    TEST_ASSERT_EQ(total_count, 10, "TS query total_count wrong");

    /* Iterate through records */
    int records_read = 0;
    ekkdb_ts_record_t rec;
    while (ekkdb_ts_server_next(iter_handle, &rec) == EKKDB_OK) {
        TEST_ASSERT_EQ(rec.module_id, 42, "TS record module_id wrong");
        records_read++;
    }
    TEST_ASSERT_EQ(records_read, 10, "TS records_read wrong");

    ekkdb_ts_server_iter_close(iter_handle);
    ekkdb_ts_server_close(handle);
    TEST_PASS();
}

static void test_ts_time_range_query(void)
{
    uart_puts("test_ts_time_range_query: ");

    int handle = ekkdb_ts_server_open(43, "temp", 0);
    TEST_ASSERT(handle >= 0, "TS open failed");

    /* Append records at specific timestamps */
    for (int i = 0; i < 20; i++) {
        ekkdb_ts_record_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp = 1000000 + i * 10000;  /* 10ms apart */
        rec.temp_mc = 25000 + i * 500;
        rec.module_id = 43;

        ekkdb_ts_server_append(handle, &rec);
    }

    /* Query middle range (records 5-14, timestamps 1050000-1140000) */
    uint32_t iter_handle, total_count;
    int ret = ekkdb_ts_server_query(handle, 1050000, 1150000, &iter_handle, &total_count);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "TS range query failed");
    TEST_ASSERT_EQ(total_count, 11, "TS range query count wrong");

    ekkdb_ts_server_iter_close(iter_handle);
    ekkdb_ts_server_close(handle);
    TEST_PASS();
}

static void test_ts_ring_buffer(void)
{
    uart_puts("test_ts_ring_buffer: ");

    /* This test verifies ring buffer wrap-around behavior */
    int handle = ekkdb_ts_server_open(44, "ring", 0);
    TEST_ASSERT(handle >= 0, "TS open failed");

    /* Append more records than max (should wrap) */
    int num_records = EKKDB_TS_MAX_RECORDS + 100;
    for (int i = 0; i < num_records; i++) {
        ekkdb_ts_record_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp = 1000000 + i * 1000;
        rec.power_mw = i;
        rec.module_id = 44;

        ekkdb_ts_server_append(handle, &rec);
    }

    /* Count should be capped at max */
    int count = ekkdb_ts_server_count(handle);
    TEST_ASSERT_EQ(count, EKKDB_TS_MAX_RECORDS, "TS ring count wrong");

    /* Query should return max records */
    uint32_t iter_handle, total_count;
    ekkdb_ts_server_query(handle, 0, 0, &iter_handle, &total_count);
    TEST_ASSERT_EQ(total_count, EKKDB_TS_MAX_RECORDS, "TS ring query count wrong");

    /* First record should be the oldest not overwritten */
    ekkdb_ts_record_t rec;
    ekkdb_ts_server_next(iter_handle, &rec);
    /* power_mw should be (num_records - max_records) */
    TEST_ASSERT_EQ(rec.power_mw, num_records - EKKDB_TS_MAX_RECORDS, "TS ring oldest wrong");

    ekkdb_ts_server_iter_close(iter_handle);
    ekkdb_ts_server_close(handle);
    TEST_PASS();
}

/* ============================================================================
 * Event Log Tests
 * ============================================================================ */

static void test_log_basic(void)
{
    uart_puts("test_log_basic: ");

    /* Open log */
    int handle = ekkdb_log_server_open(0);
    TEST_ASSERT_EQ(handle, 0, "Log open failed");

    /* Write some events */
    for (int i = 0; i < 5; i++) {
        ekkdb_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.timestamp = 1000000 + i * 1000;
        evt.severity = EKKDB_SEV_INFO;
        evt.source_type = EKKDB_SRC_MODULE;
        evt.source_id = 42;
        evt.event_code = 100 + i;
        strncpy(evt.message, "Test event", EKKDB_EVENT_MSG_LEN - 1);

        int ret = ekkdb_log_server_write(handle, &evt);
        TEST_ASSERT_EQ(ret, EKKDB_OK, "Log write failed");
    }

    /* Verify count */
    int count = ekkdb_log_server_count(handle);
    TEST_ASSERT_EQ(count, 5, "Log count wrong");

    /* Query all events */
    uint32_t iter_handle, total_count;
    int ret = ekkdb_log_server_query(handle, NULL, &iter_handle, &total_count);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "Log query failed");
    TEST_ASSERT_EQ(total_count, 5, "Log query total_count wrong");

    /* Iterate through events */
    int events_read = 0;
    ekkdb_event_t evt;
    while (ekkdb_log_server_next(iter_handle, &evt) == EKKDB_OK) {
        TEST_ASSERT_EQ(evt.severity, EKKDB_SEV_INFO, "Log event severity wrong");
        TEST_ASSERT_EQ(evt.source_id, 42, "Log event source_id wrong");
        events_read++;
    }
    TEST_ASSERT_EQ(events_read, 5, "Log events_read wrong");

    ekkdb_log_server_iter_close(iter_handle);
    ekkdb_log_server_close(handle);
    TEST_PASS();
}

static void test_log_severity_filter(void)
{
    uart_puts("test_log_severity_filter: ");

    int handle = ekkdb_log_server_open(0);
    TEST_ASSERT_EQ(handle, 0, "Log open failed");

    /* Write events with different severities */
    uint8_t severities[] = {
        EKKDB_SEV_DEBUG,
        EKKDB_SEV_INFO,
        EKKDB_SEV_WARN,
        EKKDB_SEV_ERROR,
        EKKDB_SEV_FATAL
    };

    for (int i = 0; i < 5; i++) {
        ekkdb_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.timestamp = 2000000 + i * 1000;
        evt.severity = severities[i];
        evt.source_type = EKKDB_SRC_SYSTEM;
        evt.source_id = 1;
        evt.event_code = 200 + i;

        ekkdb_log_server_write(handle, &evt);
    }

    /* Query only WARN and above */
    ekkdb_log_filter_t filter;
    memset(&filter, 0, sizeof(filter));
    filter.min_severity = EKKDB_SEV_WARN;
    filter.source_type = 0xFF;  /* Any */
    filter.source_id = 0xFF;    /* Any */

    uint32_t iter_handle, total_count;
    int ret = ekkdb_log_server_query(handle, &filter, &iter_handle, &total_count);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "Log filter query failed");
    TEST_ASSERT_EQ(total_count, 3, "Log filter count wrong (WARN, ERROR, FATAL)");

    ekkdb_log_server_iter_close(iter_handle);
    ekkdb_log_server_close(handle);
    TEST_PASS();
}

static void test_log_source_filter(void)
{
    uart_puts("test_log_source_filter: ");

    int handle = ekkdb_log_server_open(0);
    TEST_ASSERT_EQ(handle, 0, "Log open failed");

    /* Write events from different sources */
    for (int i = 0; i < 10; i++) {
        ekkdb_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.timestamp = 3000000 + i * 1000;
        evt.severity = EKKDB_SEV_INFO;
        evt.source_type = (i % 2 == 0) ? EKKDB_SRC_MODULE : EKKDB_SRC_POWER;
        evt.source_id = (i % 3);  /* 0, 1, 2 */
        evt.event_code = 300 + i;

        ekkdb_log_server_write(handle, &evt);
    }

    /* Query only MODULE source type */
    ekkdb_log_filter_t filter;
    memset(&filter, 0, sizeof(filter));
    filter.min_severity = EKKDB_SEV_DEBUG;
    filter.source_type = EKKDB_SRC_MODULE;
    filter.source_id = 0xFF;  /* Any ID */

    uint32_t iter_handle, total_count;
    int ret = ekkdb_log_server_query(handle, &filter, &iter_handle, &total_count);
    TEST_ASSERT_EQ(ret, EKKDB_OK, "Log source filter query failed");
    TEST_ASSERT_EQ(total_count, 5, "Log source filter count wrong");

    ekkdb_log_server_iter_close(iter_handle);
    ekkdb_log_server_close(handle);
    TEST_PASS();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

void ekkdb_test_run(void)
{
    uart_puts("\n========================================\n");
    uart_puts("EKKDB Unit Tests\n");
    uart_puts("========================================\n\n");

    g_tests_passed = 0;
    g_tests_failed = 0;

    /* Key-Value tests */
    uart_puts("--- Key-Value Store Tests ---\n");
    test_kv_basic();
    test_kv_multiple_keys();

    /* Time-Series tests */
    uart_puts("\n--- Time-Series Tests ---\n");
    test_ts_basic();
    test_ts_time_range_query();
    test_ts_ring_buffer();

    /* Event Log tests */
    uart_puts("\n--- Event Log Tests ---\n");
    test_log_basic();
    test_log_severity_filter();
    test_log_source_filter();

    /* Summary */
    uart_puts("\n========================================\n");
    uart_printf("Tests passed: %d\n", g_tests_passed);
    uart_printf("Tests failed: %d\n", g_tests_failed);
    uart_puts("========================================\n");

    if (g_tests_failed == 0) {
        uart_puts("ALL TESTS PASSED!\n");
    } else {
        uart_puts("SOME TESTS FAILED!\n");
    }
}
