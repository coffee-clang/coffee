#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

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

		char *name	 = strdup(entry);
		char *equals = strchr(name, '=');
		if (equals) {
			*equals	  = '\0';
			char *end = equals - 1;
			while (end > name && *end == ' ') {
				*end = '\0';
				end--;
			}
		}

		printf("%-20s registry\n", name);
		free(name);
	}

	printf("\nTotal: %zu dependencies\n", m->package.dependencies_count);
}

static void report_audit(manifest_t *m)
{
	if (m->package.dependencies_count == 0) {
		printf("No dependencies to audit.\n");
		return;
	}

	printf("Auditing dependencies...\n\n");

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry = m->package.dependencies[i];

		char *name	 = strdup(entry);
		char *equals = strchr(name, '=');
		if (equals) {
			*equals	  = '\0';
			char *end = equals - 1;
			while (end > name && *end == ' ') {
				*end = '\0';
				end--;
			}
		}

		recipe_t *r = registry_get(name);
		if (r) {
			printf("  %-20s OK (latest: %s)\n", name, r->version != nullptr ? r->version : "?");
			registry_free_recipe(r);
		} else {
			printf("  %-20s WARNING: not found in registry\n", name);
		}

		free(name);
	}
}

int64_t handle_report(options *opts)
{
	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (!m) {
		fprintf(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	char *type = NULL;
	if (opts->inputs_num > 1) {
		type = opts->inputs[1];
	}

	if (!type || strcmp(type, "deps") == 0) {
		report_deps(m);
	} else if (strcmp(type, "audit") == 0) {
		report_audit(m);
	} else {
		fprintf(stderr, "Error: Unknown report type '%s'. Supported: deps, audit\n", type);
		manifest_free(m);
		return 1;
	}

	manifest_free(m);
	return 0;
}
