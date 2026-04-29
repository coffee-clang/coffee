#include "../src/manifest.h"

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

static void test_extract_exact_version(void)
{
	TEST("exact version");
	char *name    = NULL;
	char *version = NULL;

	manifest_extract_dep_info("toml = \"1.0.0\"", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "toml") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, "1.0.0") == 0, "version mismatch");
	free(name);
	free(version);
	PASS();
}

static void test_extract_wildcard(void)
{
	TEST("wildcard version");
	char *name    = NULL;
	char *version = NULL;

	manifest_extract_dep_info("sds = \"*\"", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "sds") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, "*") == 0, "version mismatch");
	free(name);
	free(version);
	PASS();
}

static void test_extract_range(void)
{
	TEST("range version");
	char *name    = NULL;
	char *version = NULL;

	manifest_extract_dep_info("json = \">=2.0\"", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "json") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, ">=2.0") == 0, "version mismatch");
	free(name);
	free(version);
	PASS();
}

static void test_extract_no_version(void)
{
	TEST("no version (wildcard default)");
	char *name    = NULL;
	char *version = NULL;

	manifest_extract_dep_info("mylib", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "mylib") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, "*") == 0, "version should default to *");
	free(name);
	free(version);
	PASS();
}

static void test_extract_null_input(void)
{
	TEST("NULL entry");
	char *name    = (char *)0xdeadbeef;
	char *version = (char *)0xdeadbeef;

	manifest_extract_dep_info(NULL, &name, &version);
	ASSERT(name == NULL, "name should be NULL for NULL entry");
	ASSERT(version == NULL, "version should be NULL for NULL entry");
	PASS();
}

int main(void)
{
	test_extract_exact_version();
	test_extract_wildcard();
	test_extract_range();
	test_extract_no_version();
	test_extract_null_input();

	printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
	return tests_failed > 0 ? 1 : 0;
}
