#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

static sds build_include_flags(manifest_t *m)
{
	sds flags = sdsnew("-Isrc -Iinclude -I. -Ideps");
	if (m->package.name) {
		flags = sdscatfmt(flags, " -Iinclude/%s", m->package.name);
	}
	return flags;
}

int64_t handle_bench(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);

	char *dir_end     = strrchr(manifest_path, '/');
	sds   project_dir = nullptr;
	if (dir_end) {
		project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = sdsnew(".");
	}

	sdsfree(manifest_path);

	sds inc_flags = sdsempty();
	if (m) {
		inc_flags = build_include_flags(m);
	}

	sds cmd;
	if (strcmp(project_dir, ".") == 0) {
		cmd = sdscatfmt(sdsempty(), "make bench INC_FLAGS='%s'", inc_flags);
	} else {
		cmd = sdscatfmt(sdsempty(), "make -C '%s' bench INC_FLAGS='%s'", project_dir, inc_flags);
	}

	if (opts->verbose) {
		cmd = sdscatfmt(cmd, " VERBOSE=1");
	}

	sdsfree(project_dir);

	if (opts->verbose) {
		printf_safe("Running: %s\n", cmd);
	}

	i64 ret = system(cmd);
	sdsfree(cmd);
	sdsfree(inc_flags);

	if (m) {
		manifest_free(m);
	}

	if (ret != 0) {
		fprintf_safe(stderr, "Error: bench failed\n");
		return 1;
	}

	printf_safe("Bench complete.\n");
	return 0;
}
