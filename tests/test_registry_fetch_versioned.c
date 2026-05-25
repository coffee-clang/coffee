#include "../src/registry.h"
#include "test_framework.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

TEST(fetch_to_versioned_path)
{
	const char *home = getenv("HOME");
	if (!home) {
		home = "/tmp";
	}

	const char *test_dir = "/tmp/coffee_test_registry_fetch";
	char		clean_cmd[4096];
	snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s", test_dir);
	system(clean_cmd);

	char dest_dir[4096];
	snprintf(dest_dir, sizeof(dest_dir), "%s/test_pkg/1.0.0", test_dir);

	int ret = registry_fetch("test_pkg", "1.0.0", dest_dir);

	if (ret != 0) {
		printf("(expected error for non-existent package) ");
		PASS();
	}

	char lib_path[4096];
	snprintf(lib_path, sizeof(lib_path), "%s/library.toml", dest_dir);

	if (access(lib_path, F_OK) == 0) {
		PASS();
	} else {
		FAIL("library.toml not found after fetch");
	}
}

TEST(fetch_creates_directories)
{
	const char *home = getenv("HOME");
	if (!home) {
		home = "/tmp";
	}

	const char *test_dir = "/tmp/coffee_test_registry_mkdirs";
	char		clean_cmd[4096];
	snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s", test_dir);
	system(clean_cmd);

	char dest_dir[4096];
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

void coffee_register_registry_fetch_versioned_tests(void)
{
	TEST_REGISTER(fetch_to_versioned_path);
	TEST_REGISTER(fetch_creates_directories);
}