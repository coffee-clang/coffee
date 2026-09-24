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
	if (!dep_name_is_valid(name)) {
		fprintf_safe(stderr, "  Error: invalid dependency name '%s'\n", name);
		return 1;
	}
	if (!url_is_valid(url)) {
		fprintf_safe(stderr, "  Error: invalid git URL for '%s'\n", name);
		return 1;
	}
	if (ref != nullptr && !ref_is_valid(ref)) {
		fprintf_safe(stderr, "  Error: invalid git ref '%s' for '%s'\n", ref, name);
		return 1;
	}
	sds target_dir = sdscatprintf(sdsempty(), "%s/%s", global_dir, name);

	sds git_dir = sdscatprintf(sdsempty(), "%s/.git", target_dir);
	if (safe_access(git_dir, F_OK) == 0) {
		sdsfree(git_dir);
		if (verbose) {
			printf_safe("    Updating %s...\n", name);
		}
		i64 ret;
		if (ref != nullptr && ref_is_rev(ref)) {
			/* A SHA cannot be fetched as a refspec, and a shallow fetch
			 * may not contain the object: fetch everything (full), then
			 * check the rev out (lowercased to match rev-parse output). */
			char *argv1[] = { "git", "-C", target_dir, "fetch", "origin", nullptr };
			ret           = run_command(argv1, RUN_CMD_QUIET);
			if (ret == 0) {
				sds   lower   = ref_lowercase(ref);
				char *argv2[] = { "git", "-C", target_dir, "checkout", unconst(lower), nullptr };
				ret           = run_command(argv2, RUN_CMD_QUIET);
				sdsfree(lower);
			}
		} else if (ref != nullptr) {
			/* '--' before the refspec prevents git from parsing a
			 * ref that starts with '-' as an option (option injection). */
			char *argv1[] = { "git", "-C", target_dir, "fetch", "--depth", "1", "origin", "--", unconst(ref), nullptr };
			ret           = run_command(argv1, RUN_CMD_QUIET);
			if (ret == 0) {
				char *argv2[] = { "git", "-C", target_dir, "checkout", unconst(ref), nullptr };
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
	if (ref != nullptr && ref_is_rev(ref)) {
		/* `clone --branch` only accepts branches/tags, and a shallow
		 * clone cannot contain an arbitrary SHA: clone fully, then
		 * check the rev out (lowercased to match rev-parse output). */
		char *argv1[] = { "git", "clone", "--", unconst(url), target_dir, nullptr };
		ret           = run_command(argv1, RUN_CMD_QUIET);
		if (ret == 0) {
			sds   lower   = ref_lowercase(ref);
			char *argv2[] = { "git", "-C", target_dir, "checkout", unconst(lower), nullptr };
			ret           = run_command(argv2, RUN_CMD_QUIET);
			sdsfree(lower);
		}
	} else if (ref != nullptr) {
		char *argv[] = { "git", "clone", "--depth", "1", "--branch", unconst(ref), "--", unconst(url), target_dir, nullptr };
		ret          = run_command(argv, RUN_CMD_QUIET);
	} else {
		char *argv[] = { "git", "clone", "--depth", "1", "--", unconst(url), target_dir, nullptr };
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
	if (!dep_name_is_valid(name)) {
		fprintf_safe(stderr, "  Error: invalid dependency name '%s'\n", name);
		return 1;
	}
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

	if (safe_access(target, F_OK) != 0) {
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
	if (symlink(target, link_path) != 0) {
		fprintf_safe(stderr, "  Error: cannot create symlink '%s' -> '%s'\n", link_path, target);
		sdsfree(link_path);
		sdsfree(target);
		return 1;
	}

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

/*
 * True when name is declared directly in the root manifest's flat
 * dependency list (either "name = \"ver\"" or a bare inline-table key).
 * Transitive graph nodes are absent from that list.
 */
static bool is_root_dep(manifest_t *m, const char *name)
{
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		sds parsed = dep_parse_name(m->package.dependencies[i]);
		bool match = parsed != nullptr && strcmp(parsed, name) == 0;
		sdsfree(parsed);
		if (match) {
			return true;
		}
	}
	return false;
}

/* Append a dependency to the in-progress lockfile entry list. */
static void lf_record(lockfile_dep_t **deps, size_t *count, size_t *cap, const char *name, const char *path,
                      const char *commit)
{
	if (*count >= *cap) {
		size_t new_cap = *cap == 0 ? 16 : *cap * 2;
		*deps          = safe_realloc(*deps, new_cap * sizeof(lockfile_dep_t));
		*cap           = new_cap;
	}
	lockfile_dep_t *d = &(*deps)[(*count)++];
	memset(d, 0, sizeof(lockfile_dep_t));
	d->name    = sdsnew(name);
	d->path    = sdsnew(path);
	d->version = sdsnew("*");
	if (commit != nullptr) {
		d->commit = sdsnew(commit);
	}
}

/* Current HEAD of a git checkout, or nullptr. */
static sds git_head_commit(const char *dep_dir)
{
	char *rev_argv[] = { "git", "-C", unconst(dep_dir), "rev-parse", "HEAD", nullptr };
	sds   output     = run_command_capture(rev_argv, RUN_CMD_QUIET);
	if (output == nullptr) {
		return nullptr;
	}
	size_t olen = sdslen(output);
	if (olen > 0 && output[olen - 1] == '\n') {
		output[olen - 1] = '\0';
	}
	if (output[0] == '\0') {
		sdsfree(output);
		return nullptr;
	}
	return output;
}

/* Symlink deps/<name> to the global cache and record the dep in the
 * lockfile entry list.  Returns 0 on success. */
static i64 link_git_dep(const char *name, const char *global_dir, lockfile_dep_t **deps, size_t *count, size_t *cap)
{
	sds         cache_path = sdscatprintf(sdsempty(), "%s/%s", global_dir, name);
	sds         dep_dir    = sdscatprintf(sdsempty(), "deps/%s", name);
	struct stat st;
	if (lstat(dep_dir, &st) == 0) {
		char *rm_argv[] = { "rm", "-rf", dep_dir, nullptr };
		run_command(rm_argv, 0);
	}
	i64 ret = 0;
	if (symlink(cache_path, dep_dir) != 0) {
		fprintf_safe(stderr, "  Error: cannot create symlink '%s' -> '%s'\n", dep_dir, cache_path);
		ret = 1;
	} else {
		sds commit = git_head_commit(dep_dir);
		lf_record(deps, count, cap, name, cache_path, commit);
		sdsfree(commit);
	}
	sdsfree(cache_path);
	sdsfree(dep_dir);
	return ret;
}

/*
 * Materialize a dependency declared as a plain version string (no git or
 * path source) by resolving it through the registry: look up the recipe,
 * clone its recipe_url into the global deps cache, and symlink deps/<name>
 * to the cache.  Records the dep in the lockfile entry list.
 *
 * Returns 0 on success, non-zero on failure.
 */
static i64 fetch_registry_dep(const char *name, const char *global_dir, lockfile_dep_t **deps, size_t *count,
                              size_t *cap, bool verbose, bool offline)
{
	if (offline) {
		fprintf_safe(stderr, "  Error: cannot resolve '%s' from the registry in offline mode\n", name);
		return 1;
	}
	bool recipe_found = false;
	sds  url          = registry_source_url(name, &recipe_found);
	if (url == nullptr) {
		if (recipe_found) {
			fprintf_safe(stderr, "  Error: recipe for '%s' has no usable source URL\n", name);
		} else {
			fprintf_safe(stderr, "  Error: cannot resolve '%s' from the registry (unknown package or unreachable)\n",
			             name);
		}
		return 1;
	}
	i64 ret = fetch_git_dep(name, url, global_dir, nullptr, verbose);
	sdsfree(url);
	if (ret != 0) {
		return ret;
	}
	return link_git_dep(name, global_dir, deps, count, cap);
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

	lockfile_t *old_lf = lockfile_parse("Coffee.lock");

	const char *coffee_home = coffee_home_dir();
	sds         global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);
	mkdir("deps", 0755);

	printf_safe("Fetching dependencies...\n");

	i64 overall = 0;

	/* Lockfile entries accumulate across graph iterations. */
	lockfile_dep_t *lf_deps  = nullptr;
	size_t          lf_count = 0;
	size_t          lf_cap   = 0;

	/* Materialize deps until the graph stops growing: a transitive dep's
	 * own dependencies are only discoverable once it is materialized.  The
	 * graph is rebuilt per pass, so a depth-d chain costs O(d) resolutions;
	 * a dep that keeps failing is retried only on passes that still make
	 * progress, so the loop terminates. */
	bool progress = true;
	while (progress) {
		progress = false;

		dep_graph_t *g = dep_graph_create(m, old_lf, opts->offline);
		if (g == nullptr) {
			lockfile_free(old_lf);
			manifest_free(m);
			sdsfree(global_deps);
			fprintf_safe(stderr, "Error: Could not resolve dependency graph\n");
			return 1;
		}

		size_t dep_count = dep_graph_count(g);
		for (size_t gi = 1; gi < dep_count; gi++) {
			const char *dep_name = g->nodes[gi].name;
			if (dep_name == nullptr) {
				continue;
			}

			/* Already recorded in this run — skip. */
			bool recorded = false;
			for (size_t k = 0; k < lf_count; k++) {
				if (strcmp(lf_deps[k].name, dep_name) == 0) {
					recorded = true;
					break;
				}
			}
			if (recorded) {
				continue;
			}

			dependency_t *structured = find_dep(m, dep_name);
			bool          direct     = structured != nullptr || is_root_dep(m, dep_name);

			/* Transitive deps already on disk are recorded without
			 * re-fetching; direct deps are always (re)fetched so they
			 * track their configured ref. */
			if (!direct) {
				sds existing = dep_resolve_dir(dep_name);
				if (existing != nullptr) {
					sds commit = g->nodes[gi].is_git ? git_head_commit(existing) : nullptr;
					lf_record(&lf_deps, &lf_count, &lf_cap, dep_name, existing, commit);
					sdsfree(commit);
					sdsfree(existing);
					continue;
				}
				sdsfree(existing);
			}

			printf_safe("  %s\n", dep_name);

			const char *git_url = nullptr;
			const char *ref     = nullptr;
			const char *path    = nullptr;

			if (structured != nullptr && structured->git != nullptr) {
				git_url = structured->git;
				if (structured->tag) {
					ref = structured->tag;
				} else if (structured->branch) {
					ref = structured->branch;
				} else if (structured->rev) {
					ref = structured->rev;
				}
			} else if (structured != nullptr && structured->path != nullptr) {
				path = structured->path;
			} else if (g->nodes[gi].git_url != nullptr) {
				git_url = g->nodes[gi].git_url;
				ref     = g->nodes[gi].git_ref;
			} else if (g->nodes[gi].source_path != nullptr) {
				path = g->nodes[gi].source_path;
			}

			i64 ret = -1;
			if (git_url != nullptr) {
				if (opts->offline) {
					/* Offline: no network.  Record the dep only if it is
					 * already on disk; otherwise fail. */
					sds existing = dep_resolve_dir(dep_name);
					if (existing != nullptr) {
						sds commit = g->nodes[gi].is_git ? git_head_commit(existing) : nullptr;
						lf_record(&lf_deps, &lf_count, &lf_cap, dep_name, existing, commit);
						sdsfree(commit);
						sdsfree(existing);
						ret = 0;
					} else {
						fprintf_safe(stderr, "  Error: cannot fetch git dependency '%s' in offline mode\n", dep_name);
						ret = 1;
					}
				} else {
					ret = fetch_git_dep(dep_name, git_url, global_deps, ref, opts->verbose);
					if (ret == 0) {
						ret = link_git_dep(dep_name, global_deps, &lf_deps, &lf_count, &lf_cap);
					} else {
						fprintf_safe(stderr, "  Error: Failed to clone git dependency '%s' from %s\n", dep_name,
						             git_url);
					}
				}
			} else if (path != nullptr) {
				ret = fetch_path_dep(dep_name, path, "deps");
				if (ret == 0) {
					sds dep_dir = dep_resolve_dir(dep_name);
					lf_record(&lf_deps, &lf_count, &lf_cap, dep_name, dep_dir != nullptr ? dep_dir : "", nullptr);
					sdsfree(dep_dir);
				} else {
					fprintf_safe(stderr, "  Error: Failed to materialize path dependency '%s'\n", dep_name);
				}
			} else {
				/* A dep with no git or path source is declared as a plain
				 * version string; resolve it through the registry. */
				ret = fetch_registry_dep(dep_name, global_deps, &lf_deps, &lf_count, &lf_cap, opts->verbose,
				                         opts->offline);
			}

			if (ret != 0) {
				overall = 1;
				continue;
			}
			progress = true;
		}
		dep_graph_free(g);
	}

	/* Write the lockfile. */
	lockfile_t lf;
	memset(&lf, 0, sizeof(lf));
	lf.version = 1;
	if (m->package.name) {
		lf.package_name = sdsnew(m->package.name);
	}
	if (m->package.version) {
		lf.package_version = sdsnew(m->package.version);
	}
	lf.deps       = lf_deps;
	lf.deps_count = lf_count;

	/* A partial lockfile would silently omit the deps that failed to
	 * materialize and could later be consumed by a --locked build, so
	 * only a fully successful fetch updates Coffee.lock. */
	if (overall == 0) {
		lockfile_write("Coffee.lock", &lf);
	} else {
		fprintf_safe(stderr, "Fetch incomplete -- Coffee.lock left unchanged.\n");
	}

	sdsfree(lf.package_name);
	sdsfree(lf.package_version);
	for (size_t i = 0; i < lf.deps_count; i++) {
		sdsfree(lf.deps[i].name);
		sdsfree(lf.deps[i].version);
		sdsfree(lf.deps[i].path);
		sdsfree(lf.deps[i].commit);
	}
	safe_free(lf.deps);

	lockfile_free(old_lf);
	sdsfree(global_deps);
	manifest_free(m);

	if (overall == 0) {
		printf_safe("Fetch complete.\n");
	} else {
		fprintf_safe(stderr, "Fetch completed with errors.\n");
	}
	return overall;
}
