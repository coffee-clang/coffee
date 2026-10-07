#include "../build.h"
#include "../coffee.h"
#include "safe.h"

#include <stdio.h>
#include <string.h>

#include <unistd.h>

int64_t handle_run(options *opts)
{
	/* Build first: the Makefile is generated (or reused) and the
	 * default target `all` is built.  build_project resolves the
	 * executable path against the project directory. */
	sds exe_path = nullptr;
	i64 ret      = build_project(opts, "all", &exe_path);
	if (ret != 0) {
		sdsfree(exe_path);
		return ret;
	}

	if (safe_access(exe_path, X_OK) != 0) {
		fprintf_safe(stderr, "Error: Executable not found: %s\n", exe_path);
		sdsfree(exe_path);
		return 1;
	}

	/* Remaining arguments are passed to the program */
	char **args = nullptr;
	i64    argc = 0;
	if (opts->inputs_num > 1) {
		args = &opts->inputs[1];
		argc = opts->inputs_num - 1;
	}

	i64    total        = 1 + argc + 1;
	char **run_argv     = (char **)safe_malloc(sizeof(char *) * (size_t)total);
	i64    run_idx      = 0;
	run_argv[run_idx++] = exe_path;
	for (i64 i = 0; i < argc && args; i++) {
		run_argv[run_idx++] = args[i];
	}
	run_argv[run_idx] = nullptr;

	ret = run_command(run_argv, (int)opts->verbose ? RUN_CMD_VERBOSE : 0);

	sdsfree(exe_path);
	safe_free(run_argv);

	return ret;
}
