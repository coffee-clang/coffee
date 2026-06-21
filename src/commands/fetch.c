#include "../build.h"
#include "../coffee.h"
#include "../dep_graph.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"
#include "safe.h"

#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

/*
 * Clone or update a git dependency into the global deps cache,
 * checking out the configured ref (branch/tag/rev) if specified.
 */
static i64 fetch_git_dep(const char *name, const char *url, const char *global_dir, const char *ref, bool verbose)
{
	sds target_dir = sdscatprintf(sdsempty(), "%s/%s", global_dir, name);

	sds git_dir = sdscatprintf(sdsempty(), "%s/.git", target_dir);
	if (access(git_dir, F_OK) == 0) {
		sdsfree(git_dir);
		if (verbose) {
			printf_safe("    Updating %s...\n", name);
		}
		i64 ret;
		if (ref != nullptr) {
			char *argv1[] = { "git", "-C", target_dir, "fetch", "--depth", "1", "origin", (char *)ref, nullptr };
			ret           = run_command(argv1, RUN_CMD_QUIET);
			if (ret == 0) {
				char *argv2[] = { "git", "-C", target_dir, "checkout", (char *)ref, nullptr };
				ret           = run_command(argv2, RUN_CMD_QUIET);
			}
		} else {
			char *argv1[] = { "git", "-C", target_dir, "fetch", "--depth", "1", "origin", nullptr };
			ret           = run_command(argv1, RUN_CMD_QUIET);
			if (ret == 0) {
				char *argv2[] = { "git", "-C", target_dir, "reset", "--hard", "origin/HEAD", nullptr };
				ret           = run_command(argv2, RUN_CMD_QUIET);
			}
		}
		sdsfree(target_dir);
		return ret;
	}
	sdsfree(git_dir);

	rmdir(target_dir);

	i64 ret;
	if (ref != nullptr) {
		char *argv[] = { "git", "clone", "--depth", "1", "--branch", (char *)ref, (char *)url, target_dir, nullptr };
		ret          = run_command(argv, RUN_CMD_QUIET);
	} else {
		char *argv[] = { "git", "clone", "--depth", "1", (char *)url, target_dir, nullptr };
		ret          = run_command(argv, RUN_CMD_QUIET);
	}
	if (verbose) {
		printf_safe("    Cloning %s...\n", name);
	}
	sdsfree(target_dir);
	return ret;
}

/*
 * Materialize a path dependency: create a symlink from deps/<name> to the path.
 */
static i64 fetch_path_dep(const char *name, const char *path, const char *local_deps_dir)
{
	sds target = sdsnew(path);
	if (path[0] != '/') {
		char *resolved = realpath(path, nullptr);
		if (resolved == nullptr) {
			fprintf_safe(stderr, "  Error: path '%s' for '%s' does not exist\n", path, name);
			sdsfree(target);
			return 1;
		}
		sdsfree(target);
		target = sdsnew(resolved);
		safe_free(resolved);
	}

	if (access(target, F_OK) != 0) {
		fprintf_safe(stderr, "  Error: path '%s' for '%s' does not exist\n", target, name);
		sdsfree(target);
		return 1;
	}

	sds         link_path = sdscatprintf(sdsempty(), "%s/%s", local_deps_dir, name);
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		char *rm_argv[] = { "rm", "-rf", link_path, nullptr };
		run_command(rm_argv, 0);
	}
	symlink(target, link_path);

	sdsfree(link_path);
	sdsfree(target);
	return 0;
}

static dependency_t *find_dep(manifest_t *m, const char *name)
{
	for (size_t i = 0; i < m->dependencies.deps_count; i++) {
		if (m->dependencies.deps[i].name != nullptr && strcmp(m->dependencies.deps[i].name, name) == 0) {
			return &m->dependencies.deps[i];
		}
	}
	return nullptr;
}

int64_t handle_fetch(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse manifest\n");
		return 1;
	}

	lockfile_t  *old_lf = lockfile_parse("Coffee.lock");
	dep_graph_t *g      = dep_graph_create(m, old_lf, false);
	lockfile_free(old_lf);

	if (g == nullptr) {
		manifest_free(m);
		fprintf_safe(stderr, "Error: Could not resolve dependency graph\n");
		return 1;
	}

	const char *coffee_home = coffee_home_dir();
	sds         global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);
	mkdir("deps", 0755);

	printf_safe("Fetching dependencies...\n");

	i64 overall = 0;

	lockfile_t lf;
	memset(&lf, 0, sizeof(lf));
	lf.version = 1;
	if (m->package.name) {
		lf.package_name = sdsnew(m->package.name);
	}
	if (m->package.version) {
		lf.package_version = sdsnew(m->package.version);
	}

	size_t dep_count     = dep_graph_count(g);
	size_t non_root_deps = dep_count > 0 ? dep_count - 1 : 0;

	if (non_root_deps > 0) {
		lf.deps = safe_calloc(non_root_deps, sizeof(lockfile_dep_t));
		if (lf.deps == nullptr) {
			dep_graph_free(g);
			manifest_free(m);
			sdsfree(global_deps);
			return 1;
		}

		size_t dep_idx = 0;
		for (size_t gi = 1; gi < dep_count; gi++) {
			const char *dep_name = g->nodes[gi].name;
			if (dep_name == nullptr) {
				continue;
			}

			printf_safe("  %s\n", dep_name);

			dependency_t *structured = find_dep(m, dep_name);
			i64           ret        = -1;

			if (structured != nullptr && structured->git != nullptr) {
				const char *ref = nullptr;
				if (structured->tag) {
					ref = structured->tag;
				} else if (structured->branch) {
					ref = structured->branch;
				} else if (structured->rev) {
					ref = structured->rev;
				}

				ret = fetch_git_dep(dep_name, structured->git, global_deps, ref, opts->verbose);
				if (ret == 0) {
					sds         cache_path = sdscatprintf(sdsempty(), "%s/%s", global_deps, dep_name);
					sds         dep_dir    = sdscatprintf(sdsempty(), "deps/%s", dep_name);
					struct stat st;
					if (lstat(dep_dir, &st) == 0) {
						char *rm_argv[] = { "rm", "-rf", dep_dir, nullptr };
						run_command(rm_argv, 0);
					}
					symlink(cache_path, dep_dir);

					lf.deps[dep_idx].name = sdsnew(dep_name);
					lf.deps[dep_idx].path = sdsnew(cache_path);

					sds   rev_cmd = sdscatprintf(sdsempty(), "cd '%s' && git rev-parse HEAD 2>/dev/null", dep_dir);
					FILE *pipe    = popen(rev_cmd, "r");
					sdsfree(rev_cmd);
					if (pipe != nullptr) {
						char buf[128] = { 0 };
						if (fgets(buf, sizeof(buf), pipe) != nullptr) {
							size_t len = strlen(buf);
							if (len > 0 && buf[len - 1] == '\n') {
								buf[len - 1] = '\0';
							}
							if (buf[0] != '\0') {
								lf.deps[dep_idx].commit = sdsnew(buf);
							}
						}
						pclose(pipe);
					}

					lf.deps[dep_idx].version = sdsnew("*");
					sdsfree(cache_path);
					sdsfree(dep_dir);
				} else {
					fprintf_safe(stderr, "  Error: Failed to clone git dependency '%s' from %s\n", dep_name,
					             structured->git);
				}
			} else if (structured != nullptr && structured->path != nullptr) {
				ret = fetch_path_dep(dep_name, structured->path, "deps");
				if (ret == 0) {
					sds dep_dir              = dep_resolve_dir(dep_name);
					lf.deps[dep_idx].name    = sdsnew(dep_name);
					lf.deps[dep_idx].path    = dep_dir ? sdsnew(dep_dir) : sdsnew("");
					lf.deps[dep_idx].version = sdsnew("*");
					sdsfree(dep_dir);
				} else {
					fprintf_safe(stderr, "  Error: Failed to materialize path dependency '%s'\n", dep_name);
				}
			} else {
				fprintf_safe(stderr, "  Warning: '%s' has no git or path source — skipping\n", dep_name);
				ret = -1;
			}

			if (ret != 0) {
				overall = 1;
			}

			lf.deps_count = dep_idx + 1;
			dep_idx++;
		}
	}

	lockfile_write("Coffee.lock", &lf);

	sdsfree(lf.package_name);
	sdsfree(lf.package_version);
	for (size_t i = 0; i < lf.deps_count; i++) {
		sdsfree(lf.deps[i].name);
		sdsfree(lf.deps[i].version);
		sdsfree(lf.deps[i].path);
		sdsfree(lf.deps[i].commit);
	}
	safe_free(lf.deps);

	sdsfree(global_deps);
	dep_graph_free(g);
	manifest_free(m);

	if (overall == 0) {
		printf_safe("Fetch complete.\n");
	} else {
		fprintf_safe(stderr, "Fetch completed with errors.\n");
	}
	return overall;
}
