#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_lint(options *opts)
{
	printf("Linting source code...\n");

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

	const char *tidy_opts = (i64)opts->fix ? "--fix" : "";
	sds cmd = sdscatprintf(sdsempty(), "find src tests -name \"*.c\" | xargs clang-tidy %s --quiet -- %s 2>/dev/null",
	                       tidy_opts, inc_flags);
	sdsfree(inc_flags);

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	i64 ret = system(cmd);
	sdsfree(cmd);

	if (m) {
		manifest_free(m);
	}

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Linting failed. Please ensure 'clang-tidy' is installed.\n");
		return 1;
	}

	printf("Linting complete.\n");
	return 0;
}
