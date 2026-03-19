#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

int64_t handle_fetch(options *)
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

	printf("Fetching dependencies...\n");

	const char *cache_dir = getenv("HOME");
	if (!cache_dir) {
		cache_dir = "/tmp";
	}

	char deps_dir[4'096];
	snprintf(deps_dir, sizeof(deps_dir), "%s/.coffee/deps", cache_dir);
	mkdir(deps_dir, 0755);

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *package = m->package.dependencies[i];

		// Dependencies might be "name = \"*\"" or just "name"
		// This is a simple parser to get the name
		char *name   = strdup(package);
		char *equals = strchr(name, '=');
		if (equals) {
			*equals = '\0';
			// Trim spaces
			char *end = equals - 1;
			while (end > name && *end == ' ') {
				*end = '\0';
				end--;
			}
		}

		printf("  Fetching: %s\n", name);

		char pkg_dir[4'096];
		snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", deps_dir, name);

		int ret = registry_fetch(name, NULL, pkg_dir);
		if (ret != 0) {
			fprintf(stderr, "Error: Failed to fetch %s\n", name);
		}

		free(name);
	}

	manifest_free(m);
	printf("Fetching complete.\n");
	return 0;
}
