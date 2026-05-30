#include "../coffee.h"
#include "../project.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_locate_project(options *opts)
{
	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	char abs_path[PATH_MAX];
	if (realpath(manifest_path, abs_path) == NULL) {
		fprintf_safe(stderr, "Error: Could not resolve absolute path for manifest\n");
		free(manifest_path);
		return 1;
	}

	printf("{ \"root\": \"%s\" }\n", abs_path);

	free(manifest_path);
	return 0;
}
