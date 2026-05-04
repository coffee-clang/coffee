#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_test(options *opts)
{
	manifest_t *manifest	  = NULL;
	char	   *manifest_path = project_find_manifest(NULL);

	if (manifest_path) {
		manifest = manifest_parse(manifest_path);
	}

	char *dir_end	  = manifest_path ? strrchr(manifest_path, '/') : NULL;
	char *project_dir = NULL;
	if (dir_end) {
		project_dir = strndup(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = strdup(".");
	}

	free(manifest_path);

	char cmd[4096];
	int	 off;

	if (strcmp(project_dir, ".") == 0) {
		off = snprintf(cmd, sizeof(cmd), "make test");
	} else {
		off = snprintf(cmd, sizeof(cmd), "make -C '%s' test", project_dir);
	}

	free(project_dir);

	if (opts->inputs_num > 1) {
		char *test_name = opts->inputs[1];
		off += snprintf(cmd + off, sizeof(cmd) - off, " TEST_FILTER='%s'", test_name);
	}

	if (manifest) {
		manifest_free(manifest);
	}

	if ((size_t)off >= sizeof(cmd)) {
		fprintf(stderr, "Error: command too long\n");
		return 1;
	}

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	return system(cmd);
}
