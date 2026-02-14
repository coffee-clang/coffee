#include "../coffee.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_install(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf(stderr, "Error: Package name required\n");
		fprintf(stderr, "Usage: coffee install <package>\n");
		return 1;
	}

	char *package = opts->inputs[1];

	printf("Installing package: %s\n", package);

	const char *cache_dir = getenv("HOME");
	if (!cache_dir) {
		cache_dir = "/tmp";
	}

	char deps_dir[4'096];
	snprintf(deps_dir, sizeof(deps_dir), "%s/.coffee/deps", cache_dir);

	char pkg_dir[4'096];
	snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", deps_dir, package);

	char cmd[4'096];
	int  ret = snprintf(cmd, sizeof(cmd), "mkdir -p %s", deps_dir);
	if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
		return 1;
	}

	ret = system(cmd);
	if (ret != 0) {
		fprintf(stderr, "Error: Could not create deps directory\n");
		return 1;
	}

	ret = registry_fetch(package, NULL, pkg_dir);

	if (ret == 0) {
		printf("Installed: %s\n", package);
	} else {
		fprintf(stderr, "Error: Failed to install %s\n", package);
		fprintf(stderr, "Package not found in registry\n");
	}

	return ret;
}
