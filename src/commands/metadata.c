#include "../coffee.h"
#include "../coffee_features.h"
#include "../dep_graph.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_metadata(options *opts)
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

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse manifest\n");
		sdsfree(project_dir);
		return 1;
	}

	printf_safe("{\n");
	printf_safe("  \"package\": {\n");
	printf_safe("    \"name\": \"%s\",\n", m->package.name != nullptr ? m->package.name : "");
	printf_safe("    \"version\": \"%s\",\n", m->package.version != nullptr ? m->package.version : "");
	printf_safe("    \"edition\": \"%s\",\n", m->package.edition != nullptr ? m->package.edition : "");
	printf_safe("    \"description\": \"%s\",\n", m->package.description != nullptr ? m->package.description : "");
	printf_safe("    \"license\": \"%s\"\n", m->package.license != nullptr ? m->package.license : "");
	printf_safe("  },\n");

	/* Resolved features */
	resolved_features_t *rf = nullptr;
	if (m->features_count > 0) {
		rf = features_resolve(m, nullptr, 0, false, false);
	}
	if (rf != nullptr && rf->package_count > 0) {
		printf_safe("  \"features\": {\n");
		for (size_t i = 0; i < rf->package_count; i++) {
			if (rf->package_names[i] == nullptr) {
				continue;
			}
			printf_safe("    \"%s\": [", rf->package_names[i]);
			for (size_t j = 0; j < rf->packages[i].count; j++) {
				if (rf->packages[i].names[j]) {
					printf_safe("\"%s\"%s", rf->packages[i].names[j], (j < rf->packages[i].count - 1) ? ", " : "");
				}
			}
			printf_safe("]%s\n", (i < rf->package_count - 1) ? "," : "");
		}
		printf_safe("  },\n");
		features_free(rf);
	}

	/* Dependencies from manifest */
	printf_safe("  \"dependencies\": [\n");
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		printf_safe("    \"%s\"%s\n", m->package.dependencies[i], (i < m->package.dependencies_count - 1) ? "," : "");
	}
	printf_safe("  ]");

	/* Transitive dep graph */
	lockfile_t  *lf = lockfile_parse("Coffee.lock");
	dep_graph_t *g  = dep_graph_get(m, lf, true, project_dir);
	lockfile_free(lf);

	if (g != nullptr) {
		printf_safe(",\n  \"transitive_deps\": [\n");
		size_t total = dep_graph_count(g);
		bool   first = true;
		for (size_t gi = 1; gi < total; gi++) {
			const char *dep_name = g->nodes[gi].name;
			if (dep_name == nullptr) {
				continue;
			}
			/* Skip root (which is at index 0 and matches the package name) */
			if (m->package.name != nullptr && strcmp(dep_name, m->package.name) == 0) {
				continue;
			}

			if (!first) {
				printf_safe(",\n");
			}
			first = false;

			printf_safe("    {\n");
			printf_safe("      \"name\": \"%s\",\n", dep_name);
			const char *path = dep_graph_path(g, dep_name);
			if (path != nullptr) {
				printf_safe("      \"path\": \"%s\",\n", path);
			}
			printf_safe("      \"git\": %s", !!(dep_graph_is_git(g, dep_name)) ? "true" : "false");
			const char *ref = dep_graph_git_ref(g, dep_name);
			if (ref != nullptr) {
				printf_safe(",\n      \"ref\": \"%s\"", ref);
			}
			printf_safe("\n    }");
		}
		printf_safe("\n  ]\n");
		dep_graph_free(g);
	} else {
		printf_safe("\n");
	}

	printf_safe("}\n");

	manifest_free(m);
	sdsfree(project_dir);
	return 0;
}
