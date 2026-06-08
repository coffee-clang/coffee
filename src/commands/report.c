#include "../coffee.h"
#include "../dep_graph.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void report_deps(manifest_t *m)
{
	if (m->package.dependencies_count == 0) {
		printf("No dependencies declared.\n");
		return;
	}

	printf("%-20s %s\n", "NAME", "SOURCE");
	printf("%-20s %s\n", "----", "------");

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry = m->package.dependencies[i];

		sds   name   = sdsnew(entry);
		char *equals = strchr(name, '=');
		if (equals) {
			*equals   = '\0';
			char *end = equals - 1;
			while (end > name && *end == ' ') {
				*end = '\0';
				end--;
			}
		}

		/* Check if it's a git or path dep */
		bool is_git  = false;
		bool is_path = false;
		for (size_t j = 0; j < m->dependencies.deps_count; j++) {
			if (m->dependencies.deps[j].name != nullptr && strcmp(m->dependencies.deps[j].name, name) == 0) {
				is_git  = m->dependencies.deps[j].git != nullptr;
				is_path = m->dependencies.deps[j].path != nullptr;
				break;
			}
		}

		if (is_git) {
			printf("%-20s git\n", name);
		} else if (is_path) {
			printf("%-20s path\n", name);
		} else {
			printf("%-20s unknown\n", name);
		}

		sdsfree(name);
	}

	printf("\nTotal: %zu dependencies\n", m->package.dependencies_count);
}

static void report_audit(manifest_t *m)
{
	if (m->package.dependencies_count == 0) {
		printf("No dependencies to audit.\n");
		return;
	}

	lockfile_t  *lf = lockfile_parse("Coffee.lock");
	dep_graph_t *g  = dep_graph_create(m, lf, false);
	lockfile_free(lf);

	if (g == nullptr) {
		printf("Could not resolve dependency graph.\n");
		return;
	}

	printf("Auditing dependencies...\n\n");

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry = m->package.dependencies[i];

		sds   name   = sdsnew(entry);
		char *equals = strchr(name, '=');
		if (equals) {
			*equals   = '\0';
			char *end = equals - 1;
			while (end > name && *end == ' ') {
				*end = '\0';
				end--;
			}
		}

		if (dep_graph_is_git(g, name)) {
			const char *git_ref     = dep_graph_git_ref(g, name);
			const char *ref_display = git_ref != nullptr ? git_ref : "origin/HEAD";

			sds behind_str = nullptr;
			i64 behind     = dep_graph_compare_remote(g, name, &behind_str);

			if (behind == 0) {
				printf("  %-20s OK (%s up-to-date)\n", name, ref_display);
			} else if (behind > 0) {
				printf("  %-20s OUTDATED (%lld commits behind %s)\n", name, (long long)behind, ref_display);
			} else {
				printf("  %-20s %s\n", name, behind_str != nullptr ? behind_str : "?");
			}
			sdsfree(behind_str);
		} else {
			const char *path = dep_graph_path(g, name);
			if (path != nullptr) {
				printf("  %-20s OK (path: %s)\n", name, path);
			} else {
				printf("  %-20s NOT FOUND\n", name);
			}
		}

		sdsfree(name);
	}

	dep_graph_free(g);
}

int64_t handle_report(options *opts)
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

	char *type = nullptr;
	if (opts->inputs_num > 1) {
		type = opts->inputs[1];
	}

	if (type == nullptr || strcmp(type, "deps") == 0) {
		report_deps(m);
	} else if (strcmp(type, "audit") == 0) {
		report_audit(m);
	} else {
		fprintf_safe(stderr, "Error: Unknown report type '%s'. Supported: deps, audit\n", type);
		manifest_free(m);
		return 1;
	}

	manifest_free(m);
	return 0;
}
