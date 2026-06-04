#ifndef TEST_FRAMEWORK_H_
#define TEST_FRAMEWORK_H_

#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Type aliases used throughout the project */
typedef uint64_t u64;
typedef int64_t  i64;

/*
 * Coffee test framework — shared macros and runner API.
 *
 * Each test file:
 *   1. #include "test_framework.h"
 *   2. Define tests with TEST(name) { ... return 1; }
 *   3. Provide void coffee_register_tests(void) calling TEST_REGISTER for each test
 *
 * test_main.c calls all registration functions, then runs the suite.
 */

/* --- Test registration --- */

typedef i64 (*test_func_t)(void);

typedef struct {
	const char *name;
	test_func_t func;
} test_entry_t;

#define TEST_FRAMEWORK_MAX_TESTS 512

extern test_entry_t test_framework_tests[TEST_FRAMEWORK_MAX_TESTS];
extern u64          test_framework_count;

void test_framework_register(const char *name, test_func_t func);

/* Register a test in the coffee_register_tests() function */
#define TEST_REGISTER(name) test_framework_register(#name, test_fn_##name)

/* --- Test definition --- */

/* TEST(name) defines a test function named test_fn_<name> */
#define TEST(name) static i64 test_fn_##name(void)

/* --- Test result macros --- */

#define PASS()              \
	do {                    \
		printf("  PASS\n"); \
		fflush(stdout);     \
		return 1;           \
	} while (0)

#define FAIL(msg)                    \
	do {                             \
		printf("  FAIL: %s\n", msg); \
		fflush(stdout);              \
		return 0;                    \
	} while (0)

#define ASSERT(cond, msg) \
	do {                  \
		if (!(cond)) {    \
			FAIL(msg);    \
		}                 \
	} while (0)

/* --- Runner API --- */

/* Run all registered tests matching filter (nullptr or "" = all) */
i64 test_framework_run(const char *filter);

/* Print summary of all results */
void test_framework_summary(void);

/* Reset all state (for re-running) */
void test_framework_reset(void);

#endif /* TEST_FRAMEWORK_H_ */
