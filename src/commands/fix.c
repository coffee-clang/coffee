#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_fix(options *opts)
{
	printf("Attempting to automatically fix warnings...\n");

	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	char inc_flags[4096] = "-Isrc";
	if (m) {
		int off = snprintf_safe(inc_flags, sizeof(inc_flags), "-Isrc -Iinclude -I. -Ideps");
		if (m->package.name) {
			off += snprintf_safe(inc_flags + off, sizeof(inc_flags) - (size_t)off, " -Iinclude/%s", m->package.name);
		}
	}

	char cmd[8192];
	snprintf_safe(cmd, sizeof(cmd), "find src tests -name \"*.c\" | xargs clang-tidy --fix --quiet -- %s 2>/dev/null",
	              inc_flags);

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	int ret = system(cmd);

	if (m) {
		manifest_free(m);
	}

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Automatic fix failed. Please ensure 'clang-tidy' is installed and your code is "
		                     "mostly valid.\n");
		return 1;
	}

	printf("Fixes applied successfully where possible.\n");
	return 0;
}
