#include "../build.h"
#include "../coffee.h"
#include "../dep_graph.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"
#include "../version.h"
#include "safe.h"

#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

static i64 compile_binary(char *cc, sds name, sds *srcs, size_t src_cnt, sds flags, sds bin_dir, bool verbose)
{
	(void)bin_dir;

	sds outpath = sdscatprintf(sdsempty(), "%s/%s", bin_dir, name);
	if (outpath == nullptr) {
		return 1;
	}

	size_t flag_tokens = count_flag_tokens(flags);
	size_t argc_total  = 1 + flag_tokens + 2 + src_cnt + 1;
	char **argv        = (char **)safe_malloc(sizeof(char *) * argc_total);

	size_t idx  = 0;
	argv[idx++] = cc;

	size_t end_idx;
	sds    flags_copy = split_flags_to_argv(flags, argv, idx, &end_idx);
	idx               = end_idx;

	argv[idx++] = (char *)"-o";
	argv[idx++] = outpath;

	for (size_t i = 0; i < src_cnt; i++) {
		argv[idx++] = srcs[i];
	}
	argv[idx] = nullptr;

	i64 ret = run_command(argv, (int)verbose ? RUN_CMD_VERBOSE : 0);

	sdsfree(outpath);
	sdsfree(flags_copy);
	safe_free(argv);
	for (size_t i = 0; i < src_cnt; i++) {
		sdsfree(srcs[i]);
	}
	safe_free(srcs);

	return ret;
}

static i64 create_symlink(const char *target, const char *link_path)
{
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		if (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode)) {
			char *rm_argv[] = { "rm", "-rf", unconst(link_path), nullptr };
			if (run_command(rm_argv, 0) != 0) {
				return -1;
			}
		}
	}

	sds   link_copy  = sdsnew(link_path);
	char *last_slash = strrchr(link_copy, '/');
	if (last_slash) {
		*last_slash        = '\0';
		char *mkdir_argv[] = { "mkdir", "-p", link_copy, nullptr };
		sdsfree(link_copy);
		if (run_command(mkdir_argv, 0) != 0) {
			return -1;
		}
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
	{
		char *rm_argv[] = { "rm", "-rf", clone_path, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}

	{
		char *clone_argv[] = { "git", "clone", "--depth", "1", opts->git, clone_path, nullptr };
		i64   ret          = run_command(clone_argv, RUN_CMD_QUIET);
		if (ret != 0) {
			sdsfree(clone_path);
			fprintf_safe(stderr, "Error: Failed to clone %s from %s\n", package, opts->git);
			sdsfree(bin_dir);
			return 1;
		}
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
		{
			char *rm_argv[]    = { "rm", "-rf", cache_path, nullptr };
			char *mkdir_argv[] = { "mkdir", "-p", cache_path, nullptr };
			run_command(rm_argv, RUN_CMD_QUIET);
			run_command(mkdir_argv, 0);
		}

		/* Move cloned files into versioned directory */
		if (rename(clone_path, cache_path) != 0) {
			char *mv_argv[] = { "mv", clone_path, cache_path, nullptr };
			i64   mv_ret    = run_command(mv_argv, RUN_CMD_QUIET);
			if (mv_ret == 0) {
				char *rm2_argv[] = { "rm", "-rf", clone_path, nullptr };
				run_command(rm2_argv, RUN_CMD_QUIET);
			}
		}
	}
	sdsfree(clone_path);

	char *cc = getenv("CC") != nullptr ? getenv("CC") : "clang";

	if (pkg_manifest != nullptr && pkg_manifest->bin_count > 0) {
		/* Resolve dep flags for the package */
		sds dep_flags = sdsnew("-O0 -g");

		dep_graph_t *dg = dep_graph_create(pkg_manifest, nullptr, true);
		if (dg != nullptr) {
			for (size_t i = 1; i < dep_graph_count(dg); i++) {
				const char *dep_name = dep_graph_node_name(dg, i);
				if (dep_name != nullptr) {
					sds df    = dep_graph_flags(dg, dep_name);
					dep_flags = sdscatprintf(dep_flags, "%s", df);
					sdsfree(df);
				}
			}
			dep_graph_free(dg);
		} else {
			/* Fallback — resolve direct deps via filesystem */
			for (size_t i = 0; i < pkg_manifest->package.dependencies_count; i++) {
				sds dep_name = dep_parse_name(pkg_manifest->package.dependencies[i]);
				sds dep_dir  = dep_resolve_dir(dep_name);
				if (dep_dir != nullptr) {
					dep_add_flags(dep_dir, dep_name, &dep_flags, nullptr, 0);
					sdsfree(dep_dir);
				}
				sdsfree(dep_name);
			}
		}

		for (size_t i = 0; i < pkg_manifest->bin_count; i++) {
			binary_target_t *bt = &pkg_manifest->bin[i];
			if (bt->name == nullptr) {
				continue;
			}
			i64 ret = compile_binary(cc, bt->name, bt->src, bt->src_count, dep_flags, bin_dir,
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
