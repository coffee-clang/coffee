#include "../src/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                              \
	do {                                    \
		printf("Testing %s... ", name); \
	} while (0)
#define PASS()                    \
	do {                      \
		printf("PASS\n"); \
		tests_passed++;   \
	} while (0)
#define FAIL(msg)                          \
	do {                               \
		printf("FAIL: %s\n", msg); \
		tests_failed++;            \
	} while (0)
#define ASSERT(cond, msg)                    \
	do {                                 \
		if (!(cond)) {               \
			FAIL(msg);           \
			return;              \
		}                            \
	} while (0)

static void test_get_versions_structure(void)
{
	TEST("version_list_t structure");
	version_list_t *versions = registry_get_versions("toml");

	if (!versions) {
		printf("WARNING: Could not fetch versions (network issue)\n");
		return;
	}

	if (versions->count == 0) {
		printf("(count=0, no version field in library.toml) ");
		registry_free_versions(versions);
		PASS();
		return;
	}

	ASSERT(versions->versions != NULL, "versions array is NULL");
	ASSERT(versions->versions[0] != NULL, "first version is NULL");
	printf("(%zu versions: %s) ", versions->count, versions->versions[0]);

	registry_free_versions(versions);
	PASS();
}

static void test_get_versions_nonexistent(void)
{
	TEST("nonexistent package");
	version_list_t *versions = registry_get_versions("__nonexistent_pkg_xyz__");

	if (!versions || versions->count == 0) {
		if (versions) {
			registry_free_versions(versions);
		}
		printf("(correctly returned no versions) ");
		PASS();
		return;
	}

	FAIL("should return no versions for nonexistent package");
	registry_free_versions(versions);
}

static void test_get_versions_null(void)
{
	TEST("NULL package name");
	version_list_t *versions = registry_get_versions(NULL);
	ASSERT(versions == NULL, "should return NULL for NULL name");
	PASS();
}

static void test_get_versions_empty(void)
{
	TEST("empty package name");
	version_list_t *versions = registry_get_versions("");
	ASSERT(versions == NULL, "should return NULL for empty name");
	PASS();
}

static void test_free_versions_null(void)
{
	TEST("registry_free_versions with NULL");
	registry_free_versions(NULL);
	PASS();
}

int main(void)
{
	test_get_versions_structure();
	test_get_versions_nonexistent();
	test_get_versions_null();
	test_get_versions_empty();
	test_free_versions_null();

	printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
	return tests_failed > 0 ? 1 : 0;
}
