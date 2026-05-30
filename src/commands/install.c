#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

static int create_symlink(const char *target, const char *link_path)
{
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		if (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode)) {
			char cmd[8'192];
			snprintf_safe(cmd, sizeof(cmd), "rm -rf %s", link_path);
			if (system(cmd) != 0) {
				return -1;
			}
		}
	}

	char *link_copy	 = strdup(link_path);
	char *last_slash = strrchr(link_copy, '/');
	if (last_slash) {
		*last_slash = '\0';
		char cmd[8'192];
		snprintf_safe(cmd, sizeof(cmd), "mkdir -p %s", link_copy);
		free(link_copy);
		if (system(cmd) != 0) {
			return -1;
		}
	} else {
		free(link_copy);
	}

	if (symlink(target, link_path) != 0) {
		return -1;
	}

	return 0;
}

int64_t handle_install(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf(stderr, "Error: Package name required\n");
		fprintf(stderr, "Usage: coffee install <package> [version]\n");
		return 1;
	}

	char *package = opts->inputs[1];
	char *version = (opts->inputs_num > 2) ? strdup(opts->inputs[2]) : NULL;

	if (!version) {
		version_list_t *versions = registry_get_versions(package);
		if (versions && versions->count > 0) {
			version = strdup(versions->versions[0]);
			registry_free_versions(versions);
		} else {
			fprintf(stderr, "Error: Package '%s' not found in registry\n", package);
			return 1;
		}
	}

	printf("Installing package: %s@%s\n", package, version);

	const char *coffee_home = coffee_home_dir();
	char		global_deps[4'096];
	snprintf_safe(global_deps, sizeof(global_deps), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);

	char cache_path[4'096];
	snprintf_safe(cache_path, sizeof(cache_path), "%s/%s/%s", global_deps, package, version);

	int ret = registry_fetch(package, version, cache_path);
	if (ret != 0) {
		fprintf(stderr, "Error: Failed to install %s\n", package);
		fprintf(stderr, "Package not found in registry\n");
		free(version);
		return 1;
	}

	char *manifest_path = project_find_manifest(NULL);
	if (manifest_path) {
		char project_deps[4'096];
		snprintf_safe(project_deps, sizeof(project_deps), ".coffee/deps");
		mkdir(project_deps, 0755);

		char project_link_path[4'096];
		snprintf_safe(project_link_path, sizeof(project_link_path), "%s/%s/%s", project_deps, package, version);

		ret = create_symlink(cache_path, project_link_path);
		if (ret != 0) {
			fprintf(stderr, "Warning: Failed to create project symlink\n");
		} else {
			printf("Linked to project: %s\n", project_link_path);
		}
		free(manifest_path);
	}

	printf("Installed: %s@%s\n", package, version);
	free(version);
	return 0;
}
