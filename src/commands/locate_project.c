#include "../coffee.h"
#include "../project.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_locate_project(options *opts)
{
	(void)opts;
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	sds abs_path = sdsnewlen(nullptr, PATH_MAX);
	if (realpath(manifest_path, abs_path) == nullptr) {
		sdsfree(abs_path);
		fprintf_safe(stderr, "Error: Could not resolve absolute path for manifest\n");
		sdsfree(manifest_path);
		return 1;
	}

	printf_safe("{ \"root\": \"%s\" }\n", abs_path);
	sdsfree(abs_path);

	sdsfree(manifest_path);
	return 0;
}
