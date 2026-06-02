/*
 * Dummy test using the Unity test framework.
 * Demonstrates basic Unity assertions and the test lifecycle.
 */

#include "unity.h"

static int add(int a, int b)
{
	return a + b;
}

/* Forward declarations for Unity test functions */
void test_add_positive(void);
void test_add_negative(void);
void test_add_zero(void);
void test_assert_string(void);
void test_assert_true(void);

void setUp(void)
{
	/* Called before each test */
}

void tearDown(void)
{
	/* Called after each test */
}

void test_add_positive(void)
{
	TEST_ASSERT_EQUAL_INT(5, add(2, 3));
}

void test_add_negative(void)
{
	TEST_ASSERT_EQUAL_INT(-1, add(-3, 2));
}

void test_add_zero(void)
{
	TEST_ASSERT_EQUAL_INT(3, add(3, 0));
	TEST_ASSERT_EQUAL_INT(0, add(0, 0));
}

void test_assert_string(void)
{
	TEST_ASSERT_EQUAL_STRING("hello", "hello");
}

void test_assert_true(void)
{
	TEST_ASSERT_TRUE(1);
	TEST_ASSERT_TRUE(42);
	TEST_ASSERT_FALSE(0);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_add_positive);
	RUN_TEST(test_add_negative);
	RUN_TEST(test_add_zero);
	RUN_TEST(test_assert_string);
	RUN_TEST(test_assert_true);
	return UNITY_END();
}
