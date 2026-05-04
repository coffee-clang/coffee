#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_metadata(options *)
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

	printf("{\n");
	printf("  \"package\": {\n");
	printf("    \"name\": \"%s\",\n", m->package.name ? m->package.name : "");
	printf("    \"version\": \"%s\",\n", m->package.version ? m->package.version : "");
	printf("    \"edition\": \"%s\",\n", m->package.edition ? m->package.edition : "");
	printf("    \"description\": \"%s\",\n", m->package.description ? m->package.description : "");
	printf("    \"license\": \"%s\"\n", m->package.license ? m->package.license : "");
	printf("  },\n");

	printf("  \"dependencies\": [\n");
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		printf("    \"%s\"%s\n", m->package.dependencies[i], (i < m->package.dependencies_count - 1) ? "," : "");
	}
	printf("  ]\n");
	printf("}\n");

	manifest_free(m);
	return 0;
}
