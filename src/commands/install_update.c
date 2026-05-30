#include "../coffee.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

int64_t handle_install_update(options *opts)
{
	const char *home = getenv("HOME");
	if (!home) {
		home = "/tmp";
	}

	char deps_dir[4'096];
	snprintf_safe(deps_dir, sizeof(deps_dir), "%s/.coffee/deps", home);

	struct stat st;
	if (stat(deps_dir, &st) != 0) {
		printf("No packages to update.\n");
		return 0;
	}

	char *target = NULL;
	if (opts->inputs_num > 1) {
		target = opts->inputs[1];
	}

	if (target) {
		char pkg_dir[4'096];
		snprintf_safe(pkg_dir, sizeof(pkg_dir), "%s/%s", deps_dir, target);

		if (stat(pkg_dir, &st) != 0) {
			fprintf(stderr, "Error: Package '%s' is not installed\n", target);
			return 1;
		}

		printf("Updating %s...\n", target);

		char cmd[4'096];
		snprintf_safe(cmd, sizeof(cmd), "rm -rf %s", pkg_dir);
		system(cmd);

		int ret = registry_fetch(target, NULL, pkg_dir);
		if (ret != 0) {
			fprintf(stderr, "Error: Failed to update %s\n", target);
			return 1;
		}

		printf("Updated: %s\n", target);
		return 0;
	}

	DIR *dir = opendir(deps_dir);
	if (!dir) {
		printf("No packages to update.\n");
		return 0;
	}

	int			   count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}

		char pkg_dir[4'096];
		snprintf_safe(pkg_dir, sizeof(pkg_dir), "%s/%s", deps_dir, entry->d_name);

		printf("Updating %s...\n", entry->d_name);

		char cmd[4'096];
		snprintf_safe(cmd, sizeof(cmd), "rm -rf %s", pkg_dir);
		system(cmd);

		int ret = registry_fetch(entry->d_name, NULL, pkg_dir);
		if (ret != 0) {
			fprintf(stderr, "Error: Failed to update %s\n", entry->d_name);
		} else {
			printf("Updated: %s\n", entry->d_name);
		}
		count++;
	}

	closedir(dir);

	if (count == 0) {
		printf("No packages to update.\n");
	}

	return 0;
}
