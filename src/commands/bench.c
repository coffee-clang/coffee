#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

static int build_include_flags(manifest_t *m, char *buf, size_t buf_size)
{
	int off = snprintf_safe(buf, buf_size, "-Isrc -Iinclude -I. -Ideps");
	if (m->package.name) {
		off += snprintf_safe(buf + off, buf_size - (size_t)off, " -Iinclude/%s", m->package.name);
	}
	return off;
}

int64_t handle_bench(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);

	char *dir_end	  = strrchr(manifest_path, '/');
	char *project_dir = nullptr;
	if (dir_end) {
		project_dir = strndup(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = strdup(".");
	}

	free(manifest_path);

	char inc_flags[2048] = "";
	if (m) {
		build_include_flags(m, inc_flags, sizeof(inc_flags));
	}

	char cmd[4096];
	int	 off;

	if (strcmp(project_dir, ".") == 0) {
		off = snprintf_safe(cmd, sizeof(cmd), "make bench INC_FLAGS='%s'", inc_flags);
	} else {
		off = snprintf_safe(cmd, sizeof(cmd), "make -C '%s' bench INC_FLAGS='%s'", project_dir, inc_flags);
	}

	if (opts->verbose) {
		off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, " VERBOSE=1");
	}

	free(project_dir);

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	int ret = system(cmd);

	if (m) {
		manifest_free(m);
	}

	if (ret != 0) {
		fprintf_safe(stderr, "Error: bench failed\n");
		return 1;
	}

	printf("Bench complete.\n");
	return 0;
}
