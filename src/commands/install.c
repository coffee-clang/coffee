#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"
#include "../version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static i64 compile_binary(char *cc, sds bin_name, sds *src_globs, size_t src_count, sds dep_flags, sds out_dir,
                          bool verbose)
{
	/* Collect source files from globs */
	size_t src_cap  = 64;
	size_t src_cnt  = 0;
	sds   *src_list = malloc(sizeof(sds) * src_cap);

	for (size_t i = 0; i < src_count; i++) {
		glob_t gbuf;
		i64    ret = (i64)glob(src_globs[i], 0, nullptr, &gbuf);
		if (ret == 0) {
			for (size_t j = 0; j < gbuf.gl_pathc; j++) {
				if (src_cnt >= src_cap) {
					src_cap *= 2;
					src_list = realloc(src_list, sizeof(sds) * src_cap);
				}
				src_list[src_cnt++] = sdsnew(gbuf.gl_pathv[j]);
			}
			globfree(&gbuf);
		}
	}

	if (src_cnt == 0) {
		fprintf_safe(stderr, "Error: No source files for binary '%s'\n", bin_name);
		free(src_list);
		return 1;
	}

	sds outpath = sdscatprintf(sdsempty(), "%s/%s", out_dir, bin_name);

	/* Count tokens for argv */
	i64 max_tokens = 0;
	for (const char *p = dep_flags; *p != '\0'; p++) {
		if (*p == ' ') {
			max_tokens++;
		}
	}

	size_t argc_total = 1 + (size_t)max_tokens + 2 + src_cnt + 1;
	char **argv       = malloc(sizeof(char *) * (argc_total + 1));
	if (argv == nullptr) {
		for (size_t i = 0; i < src_cnt; i++) {
			sdsfree(src_list[i]);
		}
		free(src_list);
		sdsfree(outpath);
		return 1;
	}

	i64 idx     = 0;
	argv[idx++] = cc;

	sds   flags_copy = sdsdup(dep_flags);
	char *saveptr;
	char *token = strtok_r(flags_copy, " ", &saveptr);
	while (token) {
		argv[idx++] = token;
		token       = strtok_r(nullptr, " ", &saveptr);
	}

	argv[idx++] = (char *)"-o";
	argv[idx++] = outpath;

	for (size_t i = 0; i < src_cnt; i++) {
		argv[idx++] = src_list[i];
	}
	argv[idx] = nullptr;

	if (verbose) {
		printf_safe("  Compiling %s\n", bin_name);
	}

	pid_t pid = fork();
	i64   ret;

	if (pid == 0) {
		execvp(argv[0], argv);
		perror("execvp");
		exit(1);
	} else if (pid > 0) {
		int wstatus;
		waitpid(pid, &wstatus, 0);
		if (WIFEXITED(wstatus)) {
			ret = WEXITSTATUS(wstatus);
		} else {
			fprintf_safe(stderr, "Error: Compiler terminated abnormally (signal %d)\n", WTERMSIG(wstatus));
			ret = 1;
		}
	} else {
		perror("fork");
		ret = 1;
	}

	sdsfree(outpath);
	sdsfree(flags_copy);
	free(argv);
	for (size_t i = 0; i < src_cnt; i++) {
		sdsfree(src_list[i]);
	}
	free(src_list);

	return ret;
}

static i64 create_symlink(const char *target, const char *link_path)
{
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		if (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode)) {
			sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", link_path);
			if (system(cmd) != 0) {
				sdsfree(cmd);
				return -1;
			}
			sdsfree(cmd);
		}
	}

	sds   link_copy  = sdsnew(link_path);
	char *last_slash = strrchr(link_copy, '/');
	if (last_slash) {
		*last_slash = '\0';
		sds cmd     = sdscatprintf(sdsempty(), "mkdir -p %s", link_copy);
		sdsfree(link_copy);
		if (system(cmd) != 0) {
			sdsfree(cmd);
			return -1;
		}
		sdsfree(cmd);
	} else {
		sdsfree(link_copy);
	}

	if (symlink(target, link_path) != 0) {
		return -1;
	}

	return 0;
}

int64_t handle_install(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: Package name required\n");
		fprintf_safe(stderr, "Usage: coffee install --git <url> <package> [version]\n");
		return 1;
	}

	if (!opts->git) {
		fprintf_safe(stderr, "Error: --git <url> is required\n");
		fprintf_safe(stderr, "Usage: coffee install --git https://github.com/user/repo.git <package> [version]\n");
		return 1;
	}

	char       *package     = opts->inputs[1];
	const char *req_version = opts->inputs_num >= 3 ? opts->inputs[2] : nullptr;

	if (req_version != nullptr) {
		printf_safe("Installing package: %s (version %s)\n", package, req_version);
	} else {
		printf_safe("Installing package: %s\n", package);
	}

	const char *coffee_home = coffee_home_dir();
	sds         bin_dir     = sdscatprintf(sdsempty(), "%s/bin", coffee_home);
	mkdir(bin_dir, 0755);

	sds global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);

	/* Clone to temporary location first */
	sds clone_path = sdscatprintf(sdsempty(), "%s/%s", global_deps, package);
	sdsfree(global_deps);

	/* If clone already exists, remove it */
	sds cmd = sdscatprintf(sdsempty(), "rm -rf '%s' 2>/dev/null", clone_path);
	system(cmd);
	sdsfree(cmd);

	cmd     = sdscatprintf(sdsempty(), "git clone --depth 1 '%s' '%s' 2>/dev/null", opts->git, clone_path);
	i64 ret = system(cmd);
	sdsfree(cmd);
	if (ret != 0) {
		sdsfree(clone_path);
		fprintf_safe(stderr, "Error: Failed to clone %s from %s\n", package, opts->git);
		sdsfree(bin_dir);
		return 1;
	}

	/* Read the cloned library.toml to get version and [[bin]] targets */
	sds         lib_toml     = sdscatprintf(sdsempty(), "%s/library.toml", clone_path);
	manifest_t *pkg_manifest = manifest_parse(lib_toml);
	sdsfree(lib_toml);

	/* Determine the actual version and final install path */
	sds cache_path = nullptr;
	sds final_ver  = nullptr;

	if (pkg_manifest != nullptr && pkg_manifest->package.version != nullptr) {
		final_ver = sdsnew(pkg_manifest->package.version);
	} else {
		final_ver = sdsnew("*");
	}

	/* If requested version is specified and doesn't match, warn */
	if (req_version != nullptr && !version_satisfies(final_ver, req_version)) {
		fprintf_safe(stderr, "Warning: requested version %s but cloned version is %s\n", req_version, final_ver);
	}

	/* Install to versioned directory */
	{
		sds global_base = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
		cache_path      = sdscatprintf(sdsempty(), "%s/%s/%s", global_base, package, final_ver);
		sdsfree(global_base);

		/* Create versioned directory */
		sds mkdir_cmd = sdscatprintf(sdsempty(), "rm -rf '%s' 2>/dev/null && mkdir -p '%s'", cache_path, cache_path);
		system(mkdir_cmd);
		sdsfree(mkdir_cmd);

		/* Move cloned files into versioned directory */
		/* Use rename for atomicity; if that fails (cross-device), fall back to mv */
		if (rename(clone_path, cache_path) != 0) {
			sds mv_cmd = sdscatprintf(sdsempty(), "mv '%s'/* '%s'/ 2>/dev/null && rm -rf '%s'", clone_path, cache_path,
			                          clone_path);
			system(mv_cmd);
			sdsfree(mv_cmd);
		}
	}
	sdsfree(clone_path);

	char *cc = getenv("CC") != nullptr ? getenv("CC") : "clang";

	if (pkg_manifest != nullptr && pkg_manifest->bin_count > 0) {
		/* Resolve dep flags for the package */
		sds dep_flags = sdsnew("-O0 -g");

		for (size_t i = 0; i < pkg_manifest->package.dependencies_count; i++) {
			sds dep_name = dep_parse_name(pkg_manifest->package.dependencies[i]);
			sds dep_dir  = dep_resolve_dir(dep_name);
			if (dep_dir != nullptr) {
				dep_add_flags(dep_dir, dep_name, &dep_flags, nullptr, 0);
				sdsfree(dep_dir);
			}
			sdsfree(dep_name);
		}

		for (size_t i = 0; i < pkg_manifest->bin_count; i++) {
			binary_target_t *bt = &pkg_manifest->bin[i];
			if (bt->name == nullptr) {
				continue;
			}
			ret = compile_binary(cc, bt->name, bt->src, bt->src_count, dep_flags, bin_dir,
			                     (opts != nullptr && opts->verbose) != 0);
			if (ret == 0) {
				printf_safe("  Binary: %s/%s\n", bin_dir, bt->name);
			} else {
				fprintf_safe(stderr, "Error: Failed to compile binary '%s'\n", bt->name);
			}
		}
		sdsfree(dep_flags);
	} else {
		printf_safe("  Source installed to %s\n", cache_path);
	}

	manifest_free(pkg_manifest);

	/* Symlink into project deps/ directory */
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path) {
		sds project_deps = sdsnew("deps");
		mkdir(project_deps, 0755);

		sds project_link_path = sdscatprintf(sdsempty(), "%s/%s", project_deps, package);
		sdsfree(project_deps);

		i64 symret = create_symlink(cache_path, project_link_path);
		if (symret != 0) {
			fprintf_safe(stderr, "Warning: Failed to create project symlink\n");
		} else {
			printf_safe("Linked to project: %s\n", project_link_path);
		}
		sdsfree(project_link_path);
		sdsfree(manifest_path);
	}

	sdsfree(cache_path);
	sdsfree(final_ver);
	printf_safe("Installed: %s\n", package);
	sdsfree(bin_dir);
	return 0;
}
