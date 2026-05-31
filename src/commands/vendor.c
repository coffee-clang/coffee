#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

int64_t handle_vendor(options *opts)
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
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	if (m->package.dependencies_count == 0) {
		printf("No dependencies to vendor.\n");
		manifest_free(m);
		return 0;
	}

	mkdir("vendor", 0755);

	printf("Vendoring dependencies...\n");

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

		sds dest_dir = sdscatprintf(sdsempty(), "vendor/%s", name);

		printf("  Vendoring: %s\n", name);

		mkdir(dest_dir, 0755);

		int ret = registry_fetch(name, nullptr, dest_dir);
		if (ret != 0) {
			fprintf_safe(stderr, "Error: Failed to vendor %s\n", name);
		}

		sdsfree(dest_dir);
		sdsfree(name);
	}

	manifest_free(m);
	printf("Vendoring complete.\n");
	return 0;
}
