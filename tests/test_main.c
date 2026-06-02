/*
 * Test runner entry point.
 *
 * When adding a new test file, add a forward declaration and call
 * its coffee_register_tests() function below.
 */

#include "test_framework.h"

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

int main(int argc, char **argv)
{
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

	if (verbose) {
		printf("Registered %u tests\n", test_framework_count);
	}

	printf("=== Coffee Test Suite ===\n\n");

	i64 result = test_framework_run(filter);

	test_framework_summary();

	return result;
}
