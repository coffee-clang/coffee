#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>
#include <unistd.h>

int64_t handle_test(options *opts)
{
	manifest_t *manifest      = nullptr;
	char       *manifest_path = project_find_manifest(nullptr);

	if (manifest_path) {
		manifest = manifest_parse(manifest_path);
	}

	char *dir_end    = manifest_path != nullptr ? strrchr(manifest_path, '/') : nullptr;
	bool  in_project = false;
	sds   project_dir;

	if (dir_end) {
		project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
		in_project  = true;
	} else {
		project_dir = sdsnew(".");
	}

	sdsfree(manifest_path);

	/* Check that a Makefile exists before calling make */
	sds makefile_check = sdsempty();
	if (in_project) {
		makefile_check = sdscatprintf(makefile_check, "%s/Makefile", project_dir);
	} else {
		makefile_check = sdsnew("Makefile");
	}
	bool has_makefile = (access(makefile_check, F_OK) == 0);
	sdsfree(makefile_check);

	if (!has_makefile) {
		sdsfree(project_dir);
		fprintf_safe(stderr, "Error: No Makefile found. Cannot build test runner.\n");
		if (manifest) {
			manifest_free(manifest);
		}
		return 1;
	}

	sds cmd = sdsempty();

	/* Step 1: Build the test runner */
	if (in_project) {
		cmd = sdscatprintf(cmd, "make -s -C '%s' bin/tests/runner", project_dir);
	} else {
		cmd = sdscatprintf(cmd, "make -s bin/tests/runner");
	}

	if (opts->verbose) {
		printf("Building test runner: %s\n", cmd);
	}

	i64 ret = system(cmd);
	if (ret != 0) {
		sdsfree(cmd);
		sdsfree(project_dir);
		fprintf_safe(stderr, "Error: failed to build test runner\n");
		if (manifest) {
			manifest_free(manifest);
		}
		return 1;
	}

	/* Step 2: Run the test runner */
	sdsfree(cmd);
	cmd = sdsempty();

	if (in_project) {
		cmd = sdscatprintf(cmd, "'%s'/bin/tests/runner", project_dir);
	} else {
		cmd = sdscatprintf(cmd, "bin/tests/runner");
	}

	/* Pass TEST_FILTER from environment or first input arg as the filter */
	const char *test_filter = getenv("TEST_FILTER");
	if (!test_filter && opts->inputs_num > 1) {
		test_filter = opts->inputs[1];
	}
	if (test_filter != nullptr && test_filter[0] != '\0') {
		cmd = sdscatprintf(cmd, " '%s'", test_filter);
	}

	if (opts->verbose) {
		cmd = sdscatprintf(cmd, " --verbose");
	}

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	if (manifest) {
		manifest_free(manifest);
	}

	ret = system(cmd);
	sdsfree(cmd);
	sdsfree(project_dir);

	/* Propagate exit status */
	if (WIFEXITED(ret)) {
		return (int64_t)WEXITSTATUS(ret);
	}
	return 1;
}
