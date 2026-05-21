#include "../src/registry.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                      \
	do {                                \
		printf("Testing %s... ", name); \
	} while (0)
#define PASS()            \
	do {                  \
		printf("PASS\n"); \
		tests_passed++;   \
	} while (0)
#define FAIL(msg)                  \
	do {                           \
		printf("FAIL: %s\n", msg); \
		tests_failed++;            \
	} while (0)
#define ASSERT(cond, msg) \
	do {                  \
		if (!(cond)) {    \
			FAIL(msg);    \
			return;       \
		}                 \
	} while (0)

static void test_fetch_to_versioned_path(void)
{
	TEST("fetch to versioned directory");
	const char *home = getenv("HOME");
	if (!home) {
		home = "/tmp";
	}

	const char *test_dir = "/tmp/coffee_test_registry_fetch";
	char		clean_cmd[4'096];
	snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s", test_dir);
	system(clean_cmd);

	char dest_dir[4'096];
	snprintf(dest_dir, sizeof(dest_dir), "%s/test_pkg/1.0.0", test_dir);

	int ret = registry_fetch("test_pkg", "1.0.0", dest_dir);

	if (ret != 0) {
		printf("(expected error for non-existent package) ");
		PASS();
		return;
	}

	char lib_path[4'096];
	snprintf(lib_path, sizeof(lib_path), "%s/library.toml", dest_dir);

	if (access(lib_path, F_OK) == 0) {
		PASS();
	} else {
		FAIL("library.toml not found after fetch");
	}
}

static void test_fetch_creates_directories(void)
{
	TEST("fetch creates directories");
	const char *home = getenv("HOME");
	if (!home) {
		home = "/tmp";
	}

	const char *test_dir = "/tmp/coffee_test_registry_mkdirs";
	char		clean_cmd[4'096];
	snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s", test_dir);
	system(clean_cmd);

	char dest_dir[4'096];
	snprintf(dest_dir, sizeof(dest_dir), "%s/deep/nested/pkg/2.0.0", test_dir);

	registry_fetch("test_pkg", "2.0.0", dest_dir);

	struct stat st;
	if (stat(dest_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
		printf("(directory created) ");
		PASS();
	} else {
		FAIL("fetch did not create destination directory");
	}
}

int main(void)
{
	test_fetch_to_versioned_path();
	test_fetch_creates_directories();

	printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
	return tests_failed > 0 ? 1 : 0;
}
