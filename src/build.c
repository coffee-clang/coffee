#include "build.h"

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
			(void)fprintf(stderr, "Command terminated abnormally (signal %d)\n", WTERMSIG(status));
		}
		return 1;
	}

	perror("fork");
	return 1;
}

int build_project(manifest_t *manifest, build_opts_t *opts)
{
	if (!manifest || !manifest->package.name) {
		fprintf(stderr, "Error: No valid manifest found\n");
		return 1;
	}

	const char *cc		   = getenv("CC") != nullptr ? getenv("CC") : "clang";
	const char *output_dir = opts != nullptr && opts->target_dir != nullptr ? opts->target_dir : "target/debug";

	bool verbose = false;
	if (opts != nullptr) {
		verbose = opts->verbose;
	}

	char *mkdir_argv[] = {(char *)"mkdir", (char *)"-p", (char *)output_dir, NULL};
	int	  ret		   = run_command(mkdir_argv, verbose);
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
	char *flags = strdup(flags_str);

	resolved_features_t *resolved	  = NULL;
	bool				 has_features = false;
	if (opts != nullptr) {
		if (opts->features_count > 0) {
			has_features = true;
		}
		if (opts->all_features) {
			has_features = true;
		}
	}
	if (has_features) {
		const char **requested = NULL;
		if (opts->features_count > 0) {
			requested = (const char **)opts->features;
		}
		resolved =
			features_resolve(manifest, requested, opts->features_count, opts->all_features, opts->no_default_features);

		if (resolved) {
			size_t dflags_count = 0;
			char **dflags		= features_to_compiler_flags(resolved, name, &dflags_count);
			for (size_t i = 0; i < dflags_count; i++) {
				size_t new_len	 = strlen(flags) + strlen(dflags[i]) + 2;
				char  *new_flags = malloc(new_len);
				snprintf(new_flags, new_len, "%s %s", flags, dflags[i]);
				free(flags);
				free(dflags[i]);
				flags = new_flags;
			}
			free(dflags);
		}
	}

	glob_t globbuf;
	ret = glob("src/*.c", 0, NULL, &globbuf);
	if (ret != 0) {
		(void)fprintf(stderr, "Error: No source files found in src/*.c\n");
		free(flags);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	char outpath[4096];
	ret = snprintf(outpath, sizeof(outpath), "%s/%s", output_dir, name);
	if (ret < 0 || (size_t)ret >= sizeof(outpath)) {
		free(flags);
		globfree(&globbuf);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	int max_tokens = 1;
	for (const char *p = flags; *p; p++) {
		if (*p == ' ') {
			max_tokens++;
		}
	}

	int	   argc_total	 = 1 + max_tokens + 2 + (int)globbuf.gl_pathc + 1;
	char **compiler_argv = (char **)malloc(sizeof(char *) * (size_t)argc_total);
	if (!compiler_argv) {
		free(flags);
		globfree(&globbuf);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	int idx				 = 0;
	compiler_argv[idx++] = (char *)cc;

	char *flags_copy = strdup(flags);
	char *saveptr;
	char *token = strtok_r(flags_copy, " ", &saveptr);
	while (token) {
		compiler_argv[idx++] = token;
		token				 = strtok_r(NULL, " ", &saveptr);
	}

	compiler_argv[idx++] = (char *)"-o";
	compiler_argv[idx++] = outpath;

	for (size_t i = 0; i < globbuf.gl_pathc; i++) {
		compiler_argv[idx++] = globbuf.gl_pathv[i];
	}
	compiler_argv[idx] = NULL;

	free(flags);

	ret = run_command(compiler_argv, verbose);

	free(flags_copy);
	free((void *)compiler_argv);
	globfree(&globbuf);

	if (resolved) {
		features_free(resolved);
	}

	return ret;
}

int build_run(manifest_t *manifest, build_opts_t *opts, char **args, int argc)
{
	int ret = build_project(manifest, opts);
	if (ret != 0) {
		return ret;
	}

	const char *output_dir = opts != nullptr && opts->target_dir != nullptr ? opts->target_dir : "target/debug";
	const char *name	   = manifest->package.name;

	char exe_path[4096];
	ret = snprintf(exe_path, sizeof(exe_path), "%s/%s", output_dir, name);
	if (ret < 0 || (size_t)ret >= sizeof(exe_path)) {
		return 1;
	}

	if (access(exe_path, X_OK) != 0) {
		fprintf(stderr, "Error: Executable not found: %s\n", exe_path);
		return 1;
	}

	int	   total	= 1 + (args != nullptr ? argc : 0) + 1;
	char **run_argv = (char **)malloc(sizeof(char *) * (size_t)total);
	if (!run_argv) {
		return 1;
	}

	int idx			= 0;
	run_argv[idx++] = exe_path;
	for (int i = 0; i < argc && args; i++) {
		run_argv[idx++] = args[i];
	}
	run_argv[idx] = NULL;

	bool verbose = false;
	if (opts != nullptr) {
		verbose = opts->verbose;
	}
	ret = run_command(run_argv, verbose);
	free((void *)run_argv);
	return ret;
}
