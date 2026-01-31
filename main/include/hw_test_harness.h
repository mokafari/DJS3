/**
 * @file hw_test_harness.h
 * @brief Hardware test harness for DJ deck components
 * 
 * Provides hardware-level integration tests that run on the actual device.
 * Enable with CONFIG_HW_TEST_MODE=y in sdkconfig.
 * 
 * Output format is designed for LLM parsing:
 *   TEST_PASS|test_name|message
 *   TEST_FAIL|test_name|message
 *   TEST_INFO|test_name|message
 */

#ifndef HW_TEST_HARNESS_H
#define HW_TEST_HARNESS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run all hardware tests
 * 
 * Executes all test suites and outputs results in parseable format.
 * 
 * @return Number of failed tests (0 = all passed)
 */
int hw_test_run_all(void);

#ifdef __cplusplus
}
#endif

#endif /* HW_TEST_HARNESS_H */

