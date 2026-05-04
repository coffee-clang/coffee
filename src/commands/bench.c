#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_bench(options *opts)
{
	char *manifest_path = project_find_manifest(NULL);
	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	char *dir_end	  = strrchr(manifest_path, '/');
	char *project_dir = NULL;
	if (dir_end) {
		project_dir = strndup(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = strdup(".");
	}

	free(manifest_path);

	char cmd[4096];

	if (strcmp(project_dir, ".") == 0) {
		snprintf(cmd, sizeof(cmd), "make bench");
	} else {
		snprintf(cmd, sizeof(cmd), "make -C '%s' bench", project_dir);
	}

	free(project_dir);

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	return system(cmd);
}
