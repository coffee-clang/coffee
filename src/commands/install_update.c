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
	sds deps_dir = sdscatprintf(sdsempty(), "%s/deps", coffee_home_dir());

	struct stat st;
	if (stat(deps_dir, &st) != 0) {
		sdsfree(deps_dir);
		printf_safe("No packages to update.\n");
		return 0;
	}

	char *target = nullptr;
	if (opts->inputs_num > 1) {
		target = opts->inputs[1];
	}

	if (target) {
		sds pkg_dir = sdscatprintf(sdsempty(), "%s/%s", deps_dir, target);

		if (stat(pkg_dir, &st) != 0) {
			sdsfree(pkg_dir);
			sdsfree(deps_dir);
			fprintf_safe(stderr, "Error: Package '%s' is not installed\n", target);
			return 1;
		}

		printf_safe("Updating %s...\n", target);

		sds cmd = sdscatprintf(sdsempty(), "cd '%s' && git pull --ff-only 2>/dev/null", pkg_dir);
		i64 ret = system(cmd);
		sdsfree(cmd);
		sdsfree(pkg_dir);
		sdsfree(deps_dir);

		if (ret != 0) {
			fprintf_safe(stderr, "Error: Failed to update %s\n", target);
			return 1;
		}

		printf_safe("Updated: %s\n", target);
		return 0;
	}

	DIR *dir = opendir(deps_dir);
	if (dir == nullptr) {
		sdsfree(deps_dir);
		printf_safe("No packages to update.\n");
		return 0;
	}

	i64            count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
		if (entry->d_name[0] == '.') {
			continue;
		}

		sds pkg_dir = sdscatprintf(sdsempty(), "%s/%s", deps_dir, entry->d_name);

		printf_safe("Updating %s...\n", entry->d_name);

		sds cmd = sdscatprintf(sdsempty(), "cd '%s' && git pull --ff-only 2>/dev/null", pkg_dir);
		i64 ret = system(cmd);
		sdsfree(cmd);

		if (ret != 0) {
			fprintf_safe(stderr, "Error: Failed to update %s\n", entry->d_name);
		} else {
			printf_safe("Updated: %s\n", entry->d_name);
		}
		sdsfree(pkg_dir);
		count++;
	}

	closedir(dir);
	sdsfree(deps_dir);

	if (count == 0) {
		printf_safe("No packages to update.\n");
	}

	return 0;
}
