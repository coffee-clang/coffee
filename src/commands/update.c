#include "../build.h"
#include "../coffee.h"
#include "../dep_graph.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <toml.h>
#include <unistd.h>

int64_t handle_update(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	char *target = nullptr;
	if (opts->inputs_num > 1) {
		target = opts->inputs[1];
	}

	printf_safe("Updating dependencies...\n");

	/* Build dep graph with lockfile */
	lockfile_t  *lf = lockfile_parse("Coffee.lock");
	dep_graph_t *g  = dep_graph_create(m, lf, false);
	lockfile_free(lf);

	if (g == nullptr) {
		manifest_free(m);
		fprintf_safe(stderr, "Error: Could not resolve dependency graph\n");
		return 1;
	}

	/* Build a new lockfile */
	lockfile_t new_lf;
	memset(&new_lf, 0, sizeof(new_lf));
	new_lf.version = 1;
	if (m->package.name) {
		new_lf.package_name = sdsnew(m->package.name);
	}
	if (m->package.version) {
		new_lf.package_version = sdsnew(m->package.version);
	}

	size_t total_nodes = dep_graph_count(g);
	size_t dep_count   = total_nodes > 0 ? total_nodes - 1 : 0;

	if (dep_count > 0) {
		new_lf.deps = safe_calloc(dep_count, sizeof(lockfile_dep_t));
		if (new_lf.deps == nullptr) {
			dep_graph_free(g);
			manifest_free(m);
			return 1;
		}

		size_t dep_idx = 0;
		for (size_t gi = 1; gi < total_nodes; gi++) {
			const char *dep_name = g->nodes[gi].name;
			if (dep_name == nullptr) {
				continue;
			}

			/* If a target is specified, skip non-matching deps but preserve their lockfile entry */
			if (target != nullptr && strcmp(dep_name, target) != 0) {
				lockfile_t *old_lf = lockfile_parse("Coffee.lock");
				if (old_lf != nullptr) {
					lockfile_dep_t *old = lockfile_find_dep(old_lf, dep_name);
					if (old != nullptr) {
						new_lf.deps[dep_idx].name    = sdsnew(dep_name);
						new_lf.deps[dep_idx].version = old->version ? sdsnew(old->version) : sdsnew("*");
						new_lf.deps[dep_idx].path    = old->path ? sdsnew(old->path) : sdsnew("");
						new_lf.deps[dep_idx].commit  = old->commit ? sdsnew(old->commit) : nullptr;
						new_lf.deps_count            = dep_idx + 1;
						dep_idx++;
						lockfile_free(old_lf);
						continue;
					}
					lockfile_free(old_lf);
				}
				/* No old entry — skip entirely */
				continue;
			}

			printf_safe("  Updating: %s\n", dep_name);

			new_lf.deps[dep_idx].name = sdsnew(dep_name);

			if (dep_graph_is_git(g, dep_name)) {
				/* Fetch and update git dep */
				i64 ret = dep_graph_fetch_git(g, dep_name, opts->verbose);
				if (ret == 0) {
					const char *dep_path = dep_graph_path(g, dep_name);
					if (dep_path != nullptr) {
						new_lf.deps[dep_idx].path = sdsnew(dep_path);
					} else {
						new_lf.deps[dep_idx].path = sdsnew("");
					}

					const char *commit = dep_graph_commit(g, dep_name);
					if (commit != nullptr) {
						new_lf.deps[dep_idx].commit = sdsnew(commit);
					}
					new_lf.deps[dep_idx].version = sdsnew("*");
				} else {
					fprintf_safe(stderr, "  Warning: Failed to update git dep '%s'\n", dep_name);
					new_lf.deps[dep_idx].path    = sdsnew("");
					new_lf.deps[dep_idx].version = sdsnew("*");
				}
			} else {
				/* Path dep — just record the path */
				const char *dep_path = dep_graph_path(g, dep_name);
				if (dep_path != nullptr) {
					new_lf.deps[dep_idx].path = sdsnew(dep_path);
				} else {
					new_lf.deps[dep_idx].path = sdsnew("");
				}
				new_lf.deps[dep_idx].version = sdsnew("*");
			}

			new_lf.deps_count = dep_idx + 1;
			dep_idx++;
		}
	}

	i64 ret = lockfile_write("Coffee.lock", &new_lf);

	/* Cleanup */
	sdsfree(new_lf.package_name);
	sdsfree(new_lf.package_version);
	for (size_t i = 0; i < new_lf.deps_count; i++) {
		sdsfree(new_lf.deps[i].name);
		sdsfree(new_lf.deps[i].version);
		sdsfree(new_lf.deps[i].path);
		sdsfree(new_lf.deps[i].commit);
	}
	safe_free(new_lf.deps);

	dep_graph_free(g);
	manifest_free(m);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Could not write Coffee.lock\n");
		return 1;
	}

	printf_safe("Lockfile updated.\n");
	return 0;
}
