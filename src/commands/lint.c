#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <string.h>

int64_t handle_lint(options *opts)
{
	printf_safe("Linting source code...\n");

	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	sds inc_flags = sdsnew("-Isrc");
	if (m) {
		sdsfree(inc_flags);
		inc_flags = sdsnew("-Isrc -Iinclude -I. -Ideps");
		if (m->package.name) {
			inc_flags = sdscatprintf(inc_flags, " -Iinclude/%s", m->package.name);
		}
	}

	size_t fl_toks = count_flag_tokens(inc_flags);
	size_t argc    = 13 + fl_toks + 1; /* extra slot for --fix */
	char **argv    = (char **)safe_malloc(sizeof(char *) * argc);
	size_t idx     = 0;

	argv[idx++] = "find";
	argv[idx++] = "src";
	argv[idx++] = "tests";
	argv[idx++] = "-name";
	argv[idx++] = "*.c";
	argv[idx++] = "-exec";
	argv[idx++] = "clang-tidy";

	if (opts->fix) {
		argv[idx++] = "--fix";
	}

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
		fprintf_safe(stderr, "Error: Linting failed. Please ensure 'clang-tidy' is installed.\n");
		return 1;
	}

	printf_safe("Linting complete.\n");
	return 0;
}
