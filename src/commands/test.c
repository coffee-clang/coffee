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
	char  project_dir[4096];

	if (dir_end) {
		size_t len = (size_t)(dir_end - manifest_path);
		if (len >= sizeof(project_dir)) {
			len = sizeof(project_dir) - 1;
		}
		memccpy(project_dir, manifest_path, '\0', len);
		project_dir[len] = '\0';
		in_project       = true;
	} else {
		project_dir[0] = '.';
		project_dir[1] = '\0';
	}

	free(manifest_path);

	char cmd[4096];
	int  off = 0;

	/* Step 1: Build the test runner */
	if (in_project) {
		off = snprintf_safe(cmd, sizeof(cmd), "make -s -C '%s' bin/tests/runner", project_dir);
	} else {
		off = snprintf_safe(cmd, sizeof(cmd), "make -s bin/tests/runner");
	}

	if ((size_t)off >= sizeof(cmd)) {
		fprintf_safe(stderr, "Error: command too long\n");
		if (manifest) {
			manifest_free(manifest);
		}
		return 1;
	}

	if (opts->verbose) {
		printf("Building test runner: %s\n", cmd);
	}

	int ret = system(cmd);
	if (ret != 0) {
		fprintf_safe(stderr, "Error: failed to build test runner\n");
		if (manifest) {
			manifest_free(manifest);
		}
		return 1;
	}

	/* Step 2: Run the test runner */
	if (in_project) {
		off = snprintf_safe(cmd, sizeof(cmd), "'%s'/bin/tests/runner", project_dir);
	} else {
		off = snprintf_safe(cmd, sizeof(cmd), "bin/tests/runner");
	}

	/* Pass TEST_FILTER from environment or first input arg as the filter */
	const char *test_filter = getenv("TEST_FILTER");
	if (!test_filter && opts->inputs_num > 1) {
		test_filter = opts->inputs[1];
	}
	if (test_filter != nullptr && test_filter[0] != '\0') {
		off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, " '%s'", test_filter);
	}

	if (opts->verbose) {
		off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, " --verbose");
	}

	if ((size_t)off >= sizeof(cmd)) {
		fprintf_safe(stderr, "Error: command too long\n");
		if (manifest) {
			manifest_free(manifest);
		}
		return 1;
	}

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	if (manifest) {
		manifest_free(manifest);
	}

	ret = system(cmd);

	/* Propagate exit status */
	if (WIFEXITED(ret)) {
		return (int64_t)WEXITSTATUS(ret);
	}
	return 1;
}