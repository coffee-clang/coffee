#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

static i64 create_symlink(const char *target, const char *link_path)
{
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		if (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode)) {
			sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", link_path);
			if (system(cmd) != 0) {
				sdsfree(cmd);
				return -1;
			}
			sdsfree(cmd);
		}
	}

	sds   link_copy  = sdsnew(link_path);
	char *last_slash = strrchr(link_copy, '/');
	if (last_slash) {
		*last_slash = '\0';
		sds cmd     = sdscatprintf(sdsempty(), "mkdir -p %s", link_copy);
		sdsfree(link_copy);
		if (system(cmd) != 0) {
			sdsfree(cmd);
			return -1;
		}
		sdsfree(cmd);
	} else {
		sdsfree(link_copy);
	}

	if (symlink(target, link_path) != 0) {
		return -1;
	}

	return 0;
}

int64_t handle_install(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: Package name required\n");
		fprintf_safe(stderr, "Usage: coffee install <package> [version]\n");
		return 1;
	}

	char *package = opts->inputs[1];
	sds   version = (opts->inputs_num > 2) ? sdsnew(opts->inputs[2]) : nullptr;

	if (version == nullptr) {
		version_list_t *versions = registry_get_versions(package);
		if (versions != nullptr && versions->count > 0) {
			version = sdsnew(versions->versions[0]);
			registry_free_versions(versions);
		} else {
			fprintf_safe(stderr, "Error: Package '%s' not found in registry\n", package);
			return 1;
		}
	}

	printf("Installing package: %s@%s\n", package, version);

	const char *coffee_home = coffee_home_dir();
	sds         global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);

	sds cache_path = sdscatprintf(sdsempty(), "%s/%s/%s", global_deps, package, version);
	sdsfree(global_deps);

	i64 ret = registry_fetch(package, version, cache_path);
	if (ret != 0) {
		sdsfree(cache_path);
		fprintf_safe(stderr, "Error: Failed to install %s\n", package);
		fprintf_safe(stderr, "Package not found in registry\n");
		sdsfree(version);
		return 1;
	}

	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path) {
		sds project_deps = sdsnew("deps");
		mkdir(project_deps, 0755);

		sds project_link_path = sdscatprintf(sdsempty(), "%s/%s", project_deps, package);
		sdsfree(project_deps);

		i64 symret = create_symlink(cache_path, project_link_path);
		if (symret != 0) {
			fprintf_safe(stderr, "Warning: Failed to create project symlink\n");
		} else {
			printf("Linked to project: %s\n", project_link_path);
		}
		sdsfree(project_link_path);
		sdsfree(manifest_path);
	}

	sdsfree(cache_path);
	printf("Installed: %s@%s\n", package, version);
	sdsfree(version);
	return 0;
}
