/* Test for the test framework itself */
#include "test_framework.h"

void coffee_register_framework_tests(void);

TEST(framework_registers_test)
{
	const char *found = nullptr;
	for (u64 i = 0; i < test_framework_count; i++) {
		if (strcmp(test_framework_tests[i].name, "framework_registers_test") == 0) {
			found = test_framework_tests[i].name;
			break;
		}
	}
	ASSERT(found != nullptr, "test should be registered");
	PASS();
}

TEST(framework_test_count_is_reasonable)
{
	ASSERT(test_framework_count > 0, "there should be registered tests");
	ASSERT(test_framework_count <= TEST_FRAMEWORK_MAX_TESTS, "count within bounds");
	PASS();
}

TEST(framework_test_function_returns_expected)
{
	/* Directly call a test function to verify it works */
	if (test_framework_count == 0) {
		FAIL("no tests registered");
	}
	i64 result = test_framework_tests[0].func();
	/* We're testing that calling works, result depends on which test is first */
	(void)result;
	PASS();
}

TEST(framework_no_duplicate_names)
{
	for (u64 i = 0; i < test_framework_count; i++) {
		for (u64 j = i + 1; j < test_framework_count; j++) {
			if (strcmp(test_framework_tests[i].name, test_framework_tests[j].name) == 0) {
				FAIL("duplicate test name found");
				return 0;
			}
		}
	}
	PASS();
}

void coffee_register_framework_tests(void)
{
	TEST_REGISTER(framework_registers_test);
	TEST_REGISTER(framework_test_count_is_reasonable);
	TEST_REGISTER(framework_test_function_returns_expected);
	TEST_REGISTER(framework_no_duplicate_names);
}
