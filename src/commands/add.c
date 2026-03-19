#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_add(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf(stderr, "Error: No package specified\n");
		return 1;
	}

	char *package_name  = opts->inputs[1];
	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml in current directory or any parent directory\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	if (!m) {
		fprintf(stderr, "Error: Could not parse manifest at %s\n", manifest_path);
		free(manifest_path);
		return 1;
	}

	// Check if dependency already exists
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] &&
		    strncmp(m->package.dependencies[i], package_name, strlen(package_name)) == 0) {
			printf("Dependency %s already exists\n", package_name);
			manifest_free(m);
			free(manifest_path);
			return 0;
		}
	}

	// Add new dependency
	char dep_str[1'024];
	if (opts->path) {
		snprintf(dep_str, sizeof(dep_str), "%s = { path = \"%s\" }", package_name, opts->path);
	} else if (opts->git) {
		snprintf(dep_str, sizeof(dep_str), "%s = { git = \"%s\" }", package_name, opts->git);
	} else {
		snprintf(dep_str, sizeof(dep_str), "%s = \"*\"", package_name);
	}

	m->package.dependencies_count++;
	m->package.dependencies = realloc(m->package.dependencies, m->package.dependencies_count * sizeof(char *));
	m->package.dependencies[m->package.dependencies_count - 1] = strdup(dep_str);

	if (manifest_write(manifest_path, m) != 0) {
		fprintf(stderr, "Error: Could not write manifest at %s\n", manifest_path);
		manifest_free(m);
		free(manifest_path);
		return 1;
	}

	printf("Added dependency: %s\n", package_name);

	manifest_free(m);
	free(manifest_path);
	return 0;
}
