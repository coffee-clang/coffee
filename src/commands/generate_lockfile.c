#include "../build.h"
#include "../coffee.h"
#include "../dep_graph.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>
#include <unistd.h>

int64_t handle_generate_lockfile(options *opts)
{
	(void)opts;
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	/* Extract project directory from manifest path */
	sds   project_dir;
	char *dir_end = strrchr(manifest_path, '/');
	if (dir_end != nullptr) {
		project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = sdsnew(".");
	}

	manifest_t *manifest = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (manifest == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		sdsfree(project_dir);
		return 1;
	}

	printf_safe("Generating lockfile: Coffee.lock\n");

	/* Resolve full transitive graph */
	dep_graph_t *g = dep_graph_get(manifest, nullptr, true, project_dir);
	if (g == nullptr) {
		manifest_free(manifest);
		sdsfree(project_dir);
		fprintf_safe(stderr, "Error: Could not resolve dependency graph\n");
		return 1;
	}

	lockfile_t lf;
	memset(&lf, 0, sizeof(lf));
	lf.version = 1;

	if (manifest->package.name) {
		lf.package_name = sdsnew(manifest->package.name);
	}
	if (manifest->package.version) {
		lf.package_version = sdsnew(manifest->package.version);
	}

	size_t total_nodes = dep_graph_count(g);
	size_t dep_count   = total_nodes > 0 ? total_nodes - 1 : 0;

	if (dep_count > 0) {
		lf.deps = calloc(dep_count, sizeof(lockfile_dep_t));
		if (lf.deps == nullptr) {
			dep_graph_free(g);
			manifest_free(manifest);
			return 1;
		}

		size_t dep_idx = 0;
		for (size_t gi = 1; gi < total_nodes; gi++) {
			const char *dep_name = g->nodes[gi].name;
			if (dep_name == nullptr) {
				continue;
			}

			lf.deps[dep_idx].name = sdsnew(dep_name);

			/* Path */
			const char *p = dep_graph_path(g, dep_name);
			if (p != nullptr) {
				lf.deps[dep_idx].path = sdsnew(p);
			} else {
				lf.deps[dep_idx].path = sdsnew("");
			}

			/* Version */
			lf.deps[dep_idx].version = sdsnew("*");

			/* Commit SHA for git deps */
			if (dep_graph_is_git(g, dep_name)) {
				const char *dep_path = dep_graph_path(g, dep_name);
				if (dep_path != nullptr) {
					sds   cmd  = sdscatprintf(sdsempty(), "cd '%s' && git rev-parse HEAD 2>/dev/null", dep_path);
					FILE *pipe = popen(cmd, "r");
					sdsfree(cmd);
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
				}
			}

			lf.deps_count = dep_idx + 1;
			dep_idx++;
		}
	}

	i64 ret = lockfile_write("Coffee.lock", &lf);

	sdsfree(lf.package_name);
	sdsfree(lf.package_version);
	for (size_t i = 0; i < lf.deps_count; i++) {
		sdsfree(lf.deps[i].name);
		sdsfree(lf.deps[i].version);
		sdsfree(lf.deps[i].path);
		sdsfree(lf.deps[i].commit);
	}
	free(lf.deps);

	dep_graph_free(g);
	manifest_free(manifest);
	sdsfree(project_dir);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Could not write Coffee.lock\n");
		return 1;
	}

	printf_safe("Lockfile generated successfully.\n");
	return 0;
}
