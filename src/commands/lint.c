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
	free(manifest_path);

	char inc_flags[4096] = "-Isrc";
	if (m) {
		int off = snprintf_safe(inc_flags, sizeof(inc_flags), "-Isrc -Iinclude -I. -Ideps");
		if (m->package.name) {
			off += snprintf_safe(inc_flags + off, sizeof(inc_flags) - (size_t)off, " -Iinclude/%s", m->package.name);
		}
	}

	const char *tidy_opts = (int)opts->fix ? "--fix" : "";
	char        cmd[8192];
	snprintf_safe(cmd, sizeof(cmd), "find src tests -name \"*.c\" | xargs clang-tidy %s --quiet -- %s 2>/dev/null",
	              tidy_opts, inc_flags);

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	int ret = system(cmd);

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
