/**
 * @file ekkfs_test.h
 * @brief EKKFS Test Suite Header
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef EKKFS_TEST_H
#define EKKFS_TEST_H

#include <stdint.h>

/* Test result codes */
#define TEST_PASS           0
#define TEST_FAIL           1
#define TEST_SKIP           2

/* Test statistics */
typedef struct {
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
} ekkfs_test_stats_t;

/**
 * @brief Run all EKKFS tests
 *
 * Executes the complete test suite and prints results.
 *
 * @return 0 if all tests pass, number of failures otherwise
 */
int ekkfs_run_all_tests(void);

/**
 * @brief Run SD card tests only
 */
int ekkfs_test_sd(void);

/**
 * @brief Run filesystem format/mount tests
 */
int ekkfs_test_mount(void);

/**
 * @brief Run file CRUD tests
 */
int ekkfs_test_crud(void);

/**
 * @brief Run stress tests
 */
int ekkfs_test_stress(void);

/**
 * @brief Run journal tests
 */
int ekkfs_test_journal(void);

/**
 * @brief Run cache tests
 */
int ekkfs_test_cache(void);

/**
 * @brief Get test statistics
 */
const ekkfs_test_stats_t* ekkfs_test_get_stats(void);

#endif /* EKKFS_TEST_H */
