#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

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

		/* Look for git or path info in structured deps */
		bool found = false;
		for (size_t j = 0; j < m->dependencies.deps_count; j++) {
			if (m->dependencies.deps[j].name != nullptr && strcmp(m->dependencies.deps[j].name, name) == 0) {
				if (m->dependencies.deps[j].git != nullptr) {
					sds cmd = sdscatprintf(sdsempty(), "git clone --depth 1 '%s' '%s' 2>/dev/null",
					                       m->dependencies.deps[j].git, dest_dir);
					i64 ret = system(cmd);
					sdsfree(cmd);
					if (ret != 0) {
						fprintf_safe(stderr, "  Error: Failed to vendor git dep %s\n", name);
					}
				} else if (m->dependencies.deps[j].path != nullptr) {
					sds cmd =
					    sdscatprintf(sdsempty(), "cp -r '%s' '%s' 2>/dev/null", m->dependencies.deps[j].path, dest_dir);
					i64 ret = system(cmd);
					sdsfree(cmd);
					if (ret != 0) {
						fprintf_safe(stderr, "  Error: Failed to vendor path dep %s\n", name);
					}
				} else {
					fprintf_safe(stderr, "  Warning: %s has no git or path source — skipping\n", name);
				}
				found = true;
				break;
			}
		}

		if (!found) {
			fprintf_safe(stderr, "  Warning: %s has no structured dep info — skipping\n", name);
		}

		sdsfree(dest_dir);
		sdsfree(name);
	}

	manifest_free(m);
	printf("Vendoring complete.\n");
	return 0;
}
