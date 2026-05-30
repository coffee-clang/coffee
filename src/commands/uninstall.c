#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

int64_t handle_uninstall(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: No package specified\n");
		fprintf_safe(stderr, "Usage: coffee uninstall <package>\n");
		return 1;
	}

	char *package = opts->inputs[1];

	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}

	char pkg_dir[4'096];
	snprintf_safe(pkg_dir, sizeof(pkg_dir), "%s/.coffee/deps/%s", home, package);

	struct stat st;
	if (stat(pkg_dir, &st) != 0) {
		fprintf_safe(stderr, "Error: Package '%s' is not installed\n", package);
		return 1;
	}

	char cmd[4'096];
	int	 ret = snprintf_safe(cmd, sizeof(cmd), "rm -rf %s", pkg_dir);
	if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
		fprintf_safe(stderr, "Error: Path too long\n");
		return 1;
	}

	ret = system(cmd);
	if (ret != 0) {
		fprintf_safe(stderr, "Error: Failed to remove %s\n", package);
		return 1;
	}

	printf("Removed: %s\n", package);
	return 0;
}
