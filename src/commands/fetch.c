#include "../build.h"
#include "../coffee.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

/*
 * Clone or update a git dependency into the global deps cache.
 * Returns 0 on success.
 */
static i64 fetch_git_dep(const char *name, const char *url, const char *global_dir, bool verbose)
{
	sds target_dir = sdscatprintf(sdsempty(), "%s/%s", global_dir, name);

	/* git clone with shallow depth if not already cloned */
	sds git_dir = sdscatprintf(sdsempty(), "%s/.git", target_dir);
	if (access(git_dir, F_OK) == 0) {
		/* Already cloned — fetch latest */
		if (verbose) {
			printf("    Updating %s (git fetch)...\n", name);
		}
		sds cmd = sdscatprintf(sdsempty(), "cd '%s' && git fetch --depth 1 origin 2>/dev/null", target_dir);
		i64 ret = system(cmd);
		sdsfree(cmd);

		/* Update HEAD to remote default branch */
		sds cmd2 = sdscatprintf(sdsempty(), "cd '%s' && git reset --hard origin/HEAD 2>/dev/null", target_dir);
		if (ret == 0) {
			system(cmd2);
		}
		sdsfree(cmd2);
		sdsfree(git_dir);
		sdsfree(target_dir);
		return ret;
	}
	sdsfree(git_dir);

	/* Fresh clone — remove empty directory if it exists (e.g. from failed fetch) */
	rmdir(target_dir); /* ignore error — may not exist */

	sds cmd = sdscatprintf(sdsempty(), "git clone --depth 1 '%s' '%s' 2>/dev/null", url, target_dir);
	if (verbose) {
		printf("    Cloning %s...\n", name);
	}
	i64 ret = system(cmd);
	sdsfree(cmd);
	sdsfree(target_dir);
	return ret;
}

/*
 * Materialize a path dependency: create a symlink from deps/<name> to the path.
 * Returns 0 on success.
 */
static i64 fetch_path_dep(const char *name, const char *path, const char *local_deps_dir)
{
	/* Resolve relative path against current working directory */
	sds target = sdsnew(path);
	if (path[0] != '/') {
		/* Relative path — resolve via realpath */
		char *resolved = realpath(path, nullptr);
		if (resolved == nullptr) {
			fprintf_safe(stderr, "  Error: path '%s' for '%s' does not exist\n", path, name);
			sdsfree(target);
			return 1;
		}
		sdsfree(target);
		target = sdsnew(resolved);
		free(resolved);
	}

	/* Check target exists */
	if (access(target, F_OK) != 0) {
		fprintf_safe(stderr, "  Error: path '%s' for '%s' does not exist\n", target, name);
		sdsfree(target);
		return 1;
	}

	/* Create local symlink */
	sds         link_path = sdscatprintf(sdsempty(), "%s/%s", local_deps_dir, name);
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		/* Remove existing symlink/dir */
		sds rm_cmd = sdscatprintf(sdsempty(), "rm -rf %s", link_path);
		system(rm_cmd);
		sdsfree(rm_cmd);
	}
	symlink(target, link_path);

	sdsfree(link_path);
	sdsfree(target);
	return 0;
}

/*
 * Update the lockfile with git commit SHA for a dependency.
 */
static void update_lockfile_commit(const char *name, const char *dep_dir)
{
	sds         lockfile_path = sdsnew("Coffee.lock");
	lockfile_t *lf            = lockfile_parse(lockfile_path);
	if (lf == nullptr) {
		sdsfree(lockfile_path);
		return;
	}

	lockfile_dep_t *locked = lockfile_find_dep(lf, name);
	if (locked == nullptr) {
		lockfile_free(lf);
		sdsfree(lockfile_path);
		return;
	}

	/* Run git rev-parse HEAD */
	sds   cmd  = sdscatprintf(sdsempty(), "cd '%s' && git rev-parse HEAD 2>/dev/null", dep_dir);
	FILE *pipe = popen(cmd, "r");
	sdsfree(cmd);
	if (pipe == nullptr) {
		lockfile_free(lf);
		sdsfree(lockfile_path);
		return;
	}
	char buf[128] = { 0 };
	if (fgets(buf, sizeof(buf), pipe) == nullptr) {
		pclose(pipe);
		lockfile_free(lf);
		sdsfree(lockfile_path);
		return;
	}
	pclose(pipe);
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
	}
	if (buf[0] != '\0') {
		sdsfree(locked->commit);
		locked->commit = sdsnew(buf);
		lockfile_write(lockfile_path, lf);
	}

	lockfile_free(lf);
	sdsfree(lockfile_path);
}

/* Find a dependency_t by name in the structured deps */
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

	const char *coffee_home = coffee_home_dir();
	sds         global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);
	mkdir("deps", 0755);

	printf("Fetching dependencies...\n");

	i64 overall = 0;

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		/* Extract bare name from flat dependency string */
		sds name = dep_parse_name(m->package.dependencies[i]);

		printf("  %s\n", name);

		/* Check if this dep has structured info */
		dependency_t *dep = find_dep(m, name);

		i64 ret = -1;

		if (dep != nullptr && dep->git != nullptr) {
			/* Git dependency */
			ret = fetch_git_dep(name, dep->git, global_deps, (opts->verbose));
			if (ret == 0) {
				/* Create symlink: deps/<name> -> ~/.coffee/deps/<name> */
				sds         cache_path = sdscatprintf(sdsempty(), "%s/%s", global_deps, name);
				sds         dep_dir    = sdscatprintf(sdsempty(), "deps/%s", name);
				struct stat st;
				if (lstat(dep_dir, &st) == 0) {
					sds rm_cmd = sdscatprintf(sdsempty(), "rm -rf %s", dep_dir);
					system(rm_cmd);
					sdsfree(rm_cmd);
				}
				symlink(cache_path, dep_dir);
				sdsfree(cache_path);
				/* Update lockfile with commit SHA */
				update_lockfile_commit(name, dep_dir);
				sdsfree(dep_dir);
			} else {
				fprintf_safe(stderr, "  Error: Failed to clone git dependency '%s' from %s\n", name, dep->git);
			}
		} else if (dep != nullptr && dep->path != nullptr) {
			/* Path dependency — symlink directly */
			ret = fetch_path_dep(name, dep->path, "deps");
			if (ret != 0) {
				fprintf_safe(stderr, "  Error: Failed to materialize path dependency '%s'\n", name);
			}
		} else {
			/* Plain version string — try registry */
			sds cache_dir_pkg = sdscatprintf(sdsempty(), "%s/%s", global_deps, name);
			ret               = registry_fetch(name, nullptr, cache_dir_pkg);
			if (ret != 0) {
				fprintf_safe(stderr, "  Warning: Could not fetch '%s' from registry\n", name);
				fprintf_safe(stderr, "           (use --git or --path for non-registry deps)\n");
			} else {
				/* Symlink from global cache to local deps */
				sds         local_link = sdscatprintf(sdsempty(), "deps/%s", name);
				struct stat st;
				if (lstat(local_link, &st) == 0) {
					sds rm_cmd = sdscatprintf(sdsempty(), "rm -rf %s", local_link);
					system(rm_cmd);
					sdsfree(rm_cmd);
				}
				symlink(cache_dir_pkg, local_link);
				sdsfree(local_link);
			}
			sdsfree(cache_dir_pkg);
		}

		if (ret != 0) {
			overall = 1;
		}
		sdsfree(name);
	}

	sdsfree(global_deps);

	if (overall == 0) {
		printf("Fetching complete.\n");
	} else {
		fprintf_safe(stderr, "Fetching completed with errors.\n");
	}

	manifest_free(m);
	return overall;
}
