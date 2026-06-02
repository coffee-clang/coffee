#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

int64_t handle_fetch(options *opts)
{
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

	printf("Fetching dependencies...\n");

	const char *cache_dir = getenv("HOME");
	if (cache_dir == nullptr) {
		cache_dir = "/tmp";
	}

	const char *coffee_home = coffee_home_dir();
	sds         global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);

	mkdir("deps", 0755);

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *package = m->package.dependencies[i];

		// Dependencies might be "name = \"*\"" or just "name"
		// This is a simple parser to get the name
		sds   name   = sdsnew(package);
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

		sds cache_dir_pkg = sdscatprintf(sdsempty(), "%s/%s", global_deps, name);

		i64 ret = registry_fetch(name, nullptr, cache_dir_pkg);
		if (ret != 0) {
			fprintf_safe(stderr, "Error: Failed to fetch %s\n", name);
		} else {
			/* Create project-local symlink: deps/<name> -> global cache */
			sds         local_link = sdscatprintf(sdsempty(), "deps/%s", name);
			struct stat st;
			if (lstat(local_link, &st) == 0) {
				sds rm_cmd = sdscatprintf(sdsempty(), "rm -rf %s", local_link);
				system(rm_cmd);
				sdsfree(rm_cmd);
			}
			symlink(cache_dir_pkg, local_link);
			sdsfree(local_link);
		}

		sdsfree(cache_dir_pkg);
		sdsfree(name);
	}

	sdsfree(global_deps);
	manifest_free(m);
	printf("Fetching complete.\n");
	return 0;
}
