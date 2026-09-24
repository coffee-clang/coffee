/*
 * Test runner entry point.
 *
 * When adding a new test file, add a forward declaration and call
 * its coffee_register_tests() function below.
 */

#include "test_framework.h"

#include "../src/strings.h"

#include <ftw.h>
#include <unistd.h>

/* Forward declarations for all test file registration functions */
void coffee_register_features_tests(void);
void coffee_register_makefile_tests(void);
void coffee_register_cflags_libs_tests(void);
void coffee_register_manifest_version_tests(void);
void coffee_register_framework_tests(void);
void coffee_register_lockfile_tests(void);
void coffee_register_manifest_bin_tests(void);
void coffee_register_config_tests(void);
void coffee_register_doc_tests(void);
void coffee_register_install_tests(void);
void coffee_register_commands_basic_tests(void);
void coffee_register_commands_manifest_tests(void);
void coffee_register_commands_deps_tests(void);
void coffee_register_coverage_build_tests(void);
void coffee_register_coverage_commands_tests(void);
void coffee_register_coverage_manifest_tests(void);
void coffee_register_cmdline_tests(void);
void coffee_register_coverage_install_tests(void);
void coffee_register_cmdline2_tests(void);
void coffee_register_coverage_core_tests(void);
void coffee_register_coverage_build_deps_tests(void);
void coffee_register_version_tests(void);
void coffee_register_dep_graph_tests(void);
void coffee_register_new_init_tests(void);
void coffee_register_security_tests(void);
void coffee_register_manifest_deps_tests(void);

/* Remove a file/dir tree (used to clean up the per-run COFFEE_HOME). */
static int remove_tree_entry(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)st;
	(void)type;
	(void)ftw;
	return remove(path);
}

static void cleanup_test_home(void)
{
	const char *home = getenv("COFFEE_HOME");
	/* Only ever delete the sandbox we created ourselves. */
	if (home != nullptr && strncmp(home, "/tmp/coffee-test-home-", 22) == 0) {
		nftw(home, remove_tree_entry, 64, FTW_DEPTH | FTW_PHYS);
	}
}

int main(int argc, char **argv)
{
	/* Redirect COFFEE_HOME to a private sandbox so no test can ever
	 * create or delete files in the developer's real ~/.coffee. */
	sds test_home = sdsnew("/tmp/coffee-test-home-XXXXXX");
	if (mkdtemp(test_home) != nullptr) {
		setenv("COFFEE_HOME", test_home, 1);
		atexit(cleanup_test_home);
	}
	sdsfree(test_home);

	const char *filter = nullptr;

	/* Parse --test and --verbose from argv */
	i64 verbose = 0;
	for (i64 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
			verbose = 1;
		} else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
			filter = argv[++i];
		} else if (filter == nullptr && argv[i][0] != '-') {
			/* First non-flag argument is the filter */
			filter = argv[i];
		}
	}

	/* Register all tests */
	coffee_register_features_tests();
	coffee_register_makefile_tests();
	coffee_register_cflags_libs_tests();
	coffee_register_manifest_version_tests();
	coffee_register_framework_tests();
	coffee_register_lockfile_tests();
	coffee_register_manifest_bin_tests();
	coffee_register_config_tests();
	coffee_register_doc_tests();
	coffee_register_install_tests();
	coffee_register_commands_basic_tests();
	coffee_register_commands_manifest_tests();
	coffee_register_commands_deps_tests();
	coffee_register_coverage_build_tests();
	coffee_register_coverage_commands_tests();
	coffee_register_coverage_manifest_tests();
	coffee_register_cmdline_tests();
	coffee_register_coverage_install_tests();
	coffee_register_cmdline2_tests();
	coffee_register_coverage_core_tests();
	coffee_register_coverage_build_deps_tests();
	coffee_register_version_tests();
	coffee_register_dep_graph_tests();
	coffee_register_new_init_tests();
	coffee_register_security_tests();
	coffee_register_manifest_deps_tests();

	if (verbose) {
		printf("Registered %lu tests\n", (unsigned long)test_framework_count);
	}

	printf("=== Coffee Test Suite ===\n\n");

	i64 result = test_framework_run(filter);

	test_framework_summary();

	return (int)result;
}
