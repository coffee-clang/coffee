#include "test_framework.h"

#include "../src/strings.h"

/* Global test registry */
test_entry_t test_framework_tests[TEST_FRAMEWORK_MAX_TESTS];
unsigned int test_framework_count = 0;

/* Per-run results */
static int tests_run	= 0;
static int tests_passed = 0;
static int tests_failed = 0;

void test_framework_register(const char *name, test_func_t func)
{
	if (test_framework_count >= TEST_FRAMEWORK_MAX_TESTS) {
		fprintf_safe(stderr, "Error: too many tests (max %d)\n", TEST_FRAMEWORK_MAX_TESTS);
		return;
	}
	test_framework_tests[test_framework_count].name = name;
	test_framework_tests[test_framework_count].func = func;
	test_framework_count++;
}

static int test_matches_filter(const char *test_name, const char *filter)
{
	if (!filter || filter[0] == '\0') {
		return 1; /* No filter = match all */
	}
	return strstr(test_name, filter) != NULL;
}

int test_framework_run(const char *filter)
{
	int local_passed = 0;
	int local_failed = 0;

	for (unsigned int i = 0; i < test_framework_count; i++) {
		const char *name = test_framework_tests[i].name;

		if (!test_matches_filter(name, filter)) {
			continue;
		}

		printf("  %-55s ... ", name);
		fflush(stdout);

		int result = test_framework_tests[i].func();

		if (result) {
			local_passed++;
		} else {
			local_failed++;
		}

		tests_run++;
	}

	tests_passed += local_passed;
	tests_failed += local_failed;

	return local_failed > 0 ? 1 : 0;
}

void test_framework_summary(void)
{
	printf("\n=== Results: %d passed, %d failed, %d total ===\n", tests_passed, tests_failed, tests_run);
}

void test_framework_reset(void)
{
	test_framework_count = 0;
	tests_run			 = 0;
	tests_passed		 = 0;
	tests_failed		 = 0;
}
