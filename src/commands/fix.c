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
	sdsfree(manifest_path);

	sds inc_flags = sdsnew("-Isrc");
	if (m) {
		inc_flags = sdsnew("-Isrc -Iinclude -I. -Ideps");
		if (m->package.name) {
			inc_flags = sdscatprintf(inc_flags, " -Iinclude/%s", m->package.name);
		}
	}

	sds cmd = sdscatprintf(
	    sdsempty(), "find src tests -name \"*.c\" | xargs clang-tidy --fix --quiet -- %s 2>/dev/null", inc_flags);
	sdsfree(inc_flags);

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	int ret = system(cmd);
	sdsfree(cmd);

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
