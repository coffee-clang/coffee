#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
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

	char  *make_argv[16];
	size_t make_argc = 0;

	make_argv[make_argc++] = "make";
	make_argv[make_argc++] = "bench";
	if (strcmp(project_dir, ".") != 0) {
		make_argv[make_argc++] = "-C";
		make_argv[make_argc++] = project_dir;
	}
	sds inc_arg            = sdscatfmt(sdsempty(), "INC_FLAGS=%s", inc_flags);
	make_argv[make_argc++] = inc_arg;
	if (opts->verbose) {
		make_argv[make_argc++] = "VERBOSE=1";
	}
	make_argv[make_argc] = nullptr;

	if (opts->verbose) {
		printf_safe("Running: make");
		for (size_t i = 1; i < make_argc; i++) {
			printf_safe(" %s", make_argv[i]);
		}
		printf_safe("\n");
	}

	i64 ret = run_command(make_argv, 0);

	sdsfree(inc_arg);
	sdsfree(inc_flags);
	sdsfree(project_dir);

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
