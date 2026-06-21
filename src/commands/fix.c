#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <string.h>

int64_t handle_fix(options *opts)
{
	printf_safe("Attempting to automatically fix warnings...\n");

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

	/* Build argv: find src tests -name *.c -exec clang-tidy --fix --quiet {} -- <flags> + */
	size_t fl_toks = count_flag_tokens(inc_flags);
	size_t argc    = 12 + fl_toks + 1;
	char **argv    = (char **)safe_malloc(sizeof(char *) * argc);
	size_t idx     = 0;

	argv[idx++] = "find";
	argv[idx++] = "src";
	argv[idx++] = "tests";
	argv[idx++] = "-name";
	argv[idx++] = "*.c";
	argv[idx++] = "-exec";
	argv[idx++] = "clang-tidy";
	argv[idx++] = "--fix";
	argv[idx++] = "--quiet";
	argv[idx++] = "{}";
	argv[idx++] = "--";

	size_t end_idx;
	sds    flags_copy = split_flags_to_argv(inc_flags, argv, idx, &end_idx);
	idx               = end_idx;

	argv[idx++] = "+";
	argv[idx]   = nullptr;

	if (opts->verbose) {
		printf_safe("Running:");
		for (size_t i = 0; i < idx; i++) {
			printf_safe(" %s", argv[i]);
		}
		printf_safe("\n");
	}

	i64 ret = run_command(argv, 0);

	sdsfree(flags_copy);
	safe_free(argv);
	sdsfree(inc_flags);

	if (m) {
		manifest_free(m);
	}

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Automatic fix failed. Please ensure 'clang-tidy' is installed and your code is "
		                     "mostly valid.\n");
		return 1;
	}

	printf_safe("Fixes applied successfully where possible.\n");
	return 0;
}
