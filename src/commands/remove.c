#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_remove(options *opts)
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

	bool found = false;
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] &&
		    strncmp(m->package.dependencies[i], package_name, strlen(package_name)) == 0) {
			found = true;
			free(m->package.dependencies[i]);
			for (size_t j = i; j < m->package.dependencies_count - 1; j++) {
				m->package.dependencies[j] = m->package.dependencies[j + 1];
			}
			m->package.dependencies_count--;
			m->package.dependencies =
				realloc(m->package.dependencies, m->package.dependencies_count * sizeof(char *));
			break;
		}
	}

	if (!found) {
		fprintf(stderr, "Error: Dependency %s not found in manifest\n", package_name);
		manifest_free(m);
		free(manifest_path);
		return 1;
	}

	if (manifest_write(manifest_path, m) != 0) {
		fprintf(stderr, "Error: Could not write manifest at %s\n", manifest_path);
		manifest_free(m);
		free(manifest_path);
		return 1;
	}

	printf("Removed dependency: %s\n", package_name);

	manifest_free(m);
	free(manifest_path);
	return 0;
}
