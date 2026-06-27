#include "../build.h"
#include "../coffee.h"
#include "../coffee_features.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

#include <stdio.h>
#include <string.h>

#include <sys/wait.h>
#include <unistd.h>

int64_t handle_run(options *opts)
{
	char *manifest_path = nullptr;
	if (opts->manifest_path) {
		manifest_path = sdsnew(opts->manifest_path);
	} else {
		manifest_path = project_find_manifest(nullptr);
	}

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml in current directory\n");
		return 1;
	}

	/* Extract project directory from manifest path */
	char *dir_end = strrchr(manifest_path, '/');
	sds   makefile_path;
	if (dir_end) {
		size_t dir_len = (size_t)(dir_end - manifest_path) + 1;
		makefile_path  = sdscatprintf(sdsempty(), "%.*sMakefile", (int)dir_len, manifest_path);
	} else {
		makefile_path = sdsnew("Makefile");
	}

	if (access(makefile_path, F_OK) != 0) {
		sdsfree(makefile_path);
		sdsfree(manifest_path);
		fprintf_safe(stderr, "Error: No Makefile found. Coffee requires a Makefile for building.\n");
		fprintf_safe(stderr, "Use 'coffee new <name>' to create a new project with a Makefile template.\n");
		return 1;
	}

	/* Build with make */
	sds project_dir = nullptr;
	if (dir_end) {
		project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = sdsnew(".");
	}

	char  *make_argv[16];
	size_t make_argc = 0;

	make_argv[make_argc++] = "make";
	make_argv[make_argc++] = "-C";
	make_argv[make_argc++] = project_dir;

	if ((i64)opts->release) {
		make_argv[make_argc++] = "RELEASE=1";
	}
	if ((i64)opts->debug) {
		make_argv[make_argc++] = "DEBUG=1";
	}
	if (opts->jobs > 0) {
		sds jflag              = sdscatprintf(sdsempty(), "-j%lld", (long long)opts->jobs);
		make_argv[make_argc++] = jflag;
	}

	manifest_t *manifest = manifest_parse(manifest_path);
	if (manifest) {
		sds   *features       = nullptr;
		size_t features_count = 0;
		if (opts->features) {
			features_parse_cli(opts->features, &features, &features_count);
		}

		if (features_count > 0 || (i64)opts->all_features) {
			sds *requested = nullptr;
			if (features_count > 0) {
				requested = features;
			}
			resolved_features_t *resolved =
			    features_resolve(manifest, requested, features_count, opts->all_features, opts->no_default_features);
			if (resolved) {
				size_t dflags_count = 0;
				sds   *dflags       = features_to_compiler_flags(resolved, manifest->package.name, &dflags_count);
				if (dflags_count > 0) {
					sds cflags = sdsnew("CFLAGS_EXTRA=");
					for (size_t i = 0; i < dflags_count; i++) {
						cflags = sdscatprintf(cflags, "%s%s", dflags[i], (i + 1 < dflags_count) ? " " : "");
					}
					make_argv[make_argc++] = cflags;
				}
				for (size_t i = 0; i < dflags_count; i++) {
					sdsfree(dflags[i]);
				}
				safe_free(dflags);
				features_free(resolved);
			}
		}

		for (size_t i = 0; i < features_count; i++) {
			sdsfree(features[i]);
		}
		safe_free(features);
	}

	make_argv[make_argc] = nullptr;

	if (opts->verbose) {
		printf_safe("Running:");
		for (size_t i = 0; i < make_argc; i++) {
			printf_safe(" %s", make_argv[i]);
		}
		printf_safe("\n");
	}

	i64 ret = run_command(make_argv, 0);

	if (opts->jobs > 0) {
		sdsfree(make_argv[5]);
	}

	sdsfree(project_dir);
	sdsfree(makefile_path);

	if (ret != 0) {
		sdsfree(manifest_path);
		manifest_free(manifest);
		fprintf_safe(stderr, "Build failed (make exited with code %d)\n", (int)ret);
		return ret;
	}

	/* Run the built binary */
	const char *name       = manifest->package.name;
	const char *output_dir = opts->target_dir ? opts->target_dir : "target/debug";
	sds         exe_path   = sdscatprintf(sdsempty(), "%s/%s", output_dir, name);

	if (access(exe_path, X_OK) != 0) {
		fprintf_safe(stderr, "Error: Executable not found: %s\n", exe_path);
		sdsfree(exe_path);
		sdsfree(manifest_path);
		manifest_free(manifest);
		return 1;
	}

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
	sdsfree(manifest_path);
	manifest_free(manifest);

	return ret;
}
