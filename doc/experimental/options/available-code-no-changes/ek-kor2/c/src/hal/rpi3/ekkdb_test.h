/**
 * @file ekkdb_test.h
 * @brief EKKDB Unit Test Header
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_EKKDB_TEST_H
#define RPI3_EKKDB_TEST_H

/**
 * @brief Run all EKKDB unit tests
 *
 * Outputs test results to UART.
 * Should be called after filesystem and database servers are initialized.
 */
void ekkdb_test_run(void);

#endif /* RPI3_EKKDB_TEST_H */
