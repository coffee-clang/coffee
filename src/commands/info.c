#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_info(options *opts)
{
	(void)opts;
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

	printf("Package Information:\n");
	if (m->package.name) {
		printf("  Name:        %s\n", m->package.name);
	}
	if (m->package.version) {
		printf("  Version:     %s\n", m->package.version);
	}
	if (m->package.edition) {
		printf("  Edition:     %s\n", m->package.edition);
	}
	if (m->package.description) {
		printf("  Description: %s\n", m->package.description);
	}
	if (m->package.license) {
		printf("  License:     %s\n", m->package.license);
	}

	if (m->package.dependencies_count > 0) {
		printf("  Dependencies (%zu):\n", m->package.dependencies_count);
		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			printf("    - %s\n", m->package.dependencies[i]);
		}
	}

	manifest_free(m);
	return 0;
}
