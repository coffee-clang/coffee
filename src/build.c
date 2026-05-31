#include "build.h"

#include "../deps/sds/sds.h"
#include "strings.h"

#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_command(char **argv, bool verbose)
{
	if (verbose) {
		printf("Running:");
		for (char **a = argv; *a; a++) {
			printf(" %s", *a);
		}
		printf("\n");
	}

	pid_t pid = fork();

	if (pid == 0) {
		execvp(argv[0], argv);
		perror("execvp");
		exit(1);
	} else if (pid > 0) {
		int status;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status)) {
			return WEXITSTATUS(status);
		}
		if (verbose) {
			fprintf_safe(stderr, "Command terminated abnormally (signal %d)\n", WTERMSIG(status));
		}
		return 1;
	}

	perror("fork");
	return 1;
}

int build_project(manifest_t *manifest, build_opts_t *opts)
{
	if (manifest == nullptr || manifest->package.name == nullptr) {
		fprintf_safe(stderr, "Error: No valid manifest found\n");
		return 1;
	}

	const char *cc         = getenv("CC") != nullptr ? getenv("CC") : "clang";
	const char *output_dir = opts != nullptr && opts->target_dir != nullptr ? opts->target_dir : "target/debug";

	bool verbose = false;
	if (opts != nullptr) {
		verbose = opts->verbose;
	}

	char *mkdir_argv[] = { (char *)"mkdir", (char *)"-p", (char *)output_dir, nullptr };
	int   ret          = run_command(mkdir_argv, verbose);
	if (ret != 0) {
		return 1;
	}

	const char *name = manifest->package.name;

	const char *flags_str = "-O0 -g";
	if (opts != nullptr) {
		if (opts->release) {
			flags_str = "-O2";
		} else if (opts->debug) {
			flags_str = "-g";
		}
	}
	sds flags = sdsnew(flags_str);

	resolved_features_t *resolved     = nullptr;
	bool                 has_features = false;
	if (opts != nullptr) {
		if (opts->features_count > 0) {
			has_features = true;
		}
		if (opts->all_features) {
			has_features = true;
		}
	}
	if (has_features) {
		const char **requested = nullptr;
		if (opts->features_count > 0) {
			requested = (const char **)opts->features;
		}
		resolved =
		    features_resolve(manifest, requested, opts->features_count, opts->all_features, opts->no_default_features);

		if (resolved) {
			size_t dflags_count = 0;
			sds   *dflags       = features_to_compiler_flags(resolved, name, &dflags_count);
			for (size_t i = 0; i < dflags_count; i++) {
				flags = sdscatfmt(flags, " %s", dflags[i]);
				sdsfree(dflags[i]);
			}
			free((void *)dflags);
		}
	}

	glob_t globbuf;
	ret = glob("src/*.c", 0, nullptr, &globbuf);
	if (ret != 0) {
		fprintf_safe(stderr, "Error: No source files found in src/*.c\n");
		sdsfree(flags);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	sds outpath = sdscatprintf(sdsempty(), "%s/%s", output_dir, name);
	if (outpath == nullptr) {
		sdsfree(flags);
		globfree(&globbuf);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	int max_tokens = 1;
	for (const char *p = flags; *p != '\0'; p++) {
		if (*p == ' ') {
			max_tokens++;
		}
	}

	int    argc_total    = 1 + max_tokens + 2 + (int)globbuf.gl_pathc + 1;
	char **compiler_argv = (char **)malloc(sizeof(char *) * ((size_t)argc_total + 1));
	if (compiler_argv == nullptr) {
		sdsfree(flags);
		globfree(&globbuf);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	int idx              = 0;
	compiler_argv[idx++] = (char *)cc;

	sds   flags_copy = sdsdup(flags);
	char *saveptr;
	char *token = strtok_r(flags_copy, " ", &saveptr);
	while (token) {
		compiler_argv[idx++] = token;
		token                = strtok_r(nullptr, " ", &saveptr);
	}

	compiler_argv[idx++] = (char *)"-o";
	compiler_argv[idx++] = outpath;

	for (size_t i = 0; i < globbuf.gl_pathc; i++) {
		compiler_argv[idx++] = globbuf.gl_pathv[i];
	}
	compiler_argv[idx] = nullptr;

	sdsfree(flags);

	ret = run_command(compiler_argv, verbose);

	sdsfree(outpath);
	sdsfree(flags_copy);
	free((void *)compiler_argv);
	globfree(&globbuf);

	if (resolved) {
		features_free(resolved);
	}

	return ret;
}

int build_run(manifest_t *manifest, build_opts_t *opts, sds *args, int argc)
{
	int ret = build_project(manifest, opts);
	if (ret != 0) {
		return ret;
	}

	const char *output_dir = opts != nullptr && opts->target_dir != nullptr ? opts->target_dir : "target/debug";
	const char *name       = manifest->package.name;

	sds exe_path = sdscatprintf(sdsempty(), "%s/%s", output_dir, name);
	if (exe_path == nullptr) {
		return 1;
	}

	if (access(exe_path, X_OK) != 0) {
		fprintf_safe(stderr, "Error: Executable not found: %s\n", exe_path);
		sdsfree(exe_path);
		return 1;
	}

	int    total    = 1 + (args != nullptr ? argc : 0) + 1;
	char **run_argv = (char **)malloc(sizeof(char *) * (size_t)total);
	if (run_argv == nullptr) {
		sdsfree(exe_path);
		return 1;
	}

	int idx         = 0;
	run_argv[idx++] = exe_path;
	for (int i = 0; i < argc && args; i++) {
		run_argv[idx++] = args[i];
	}
	run_argv[idx] = nullptr;

	bool verbose = false;
	if (opts != nullptr) {
		verbose = opts->verbose;
	}
	ret = run_command(run_argv, verbose);
	sdsfree(exe_path);
	free((void *)run_argv);
	return ret;
}
