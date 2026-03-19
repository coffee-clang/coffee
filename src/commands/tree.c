#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_tree(options *)
{
	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (!m) {
		fprintf(stderr, "Error: Could not parse manifest\n");
		return 1;
	}

	const char *name    = m->package.name ? m->package.name : "project";
	const char *version = m->package.version ? m->package.version : "0.1.0";

	printf("%s v%s\n", name, version);

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *prefix = (i == m->package.dependencies_count - 1) ? "└── " : "├── ";
		printf("%s%s\n", prefix, m->package.dependencies[i]);
	}

	manifest_free(m);
	return 0;
}
