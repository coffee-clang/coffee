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

	sds pkg_dir = sdscatprintf(sdsempty(), "%s/.coffee/deps/%s", home, package);

	struct stat st;
	if (stat(pkg_dir, &st) != 0) {
		sdsfree(pkg_dir);
		fprintf_safe(stderr, "Error: Package '%s' is not installed\n", package);
		return 1;
	}

	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", pkg_dir);
	sdsfree(pkg_dir);

	i64 ret = system(cmd);
	sdsfree(cmd);
	if (ret != 0) {
		fprintf_safe(stderr, "Error: Failed to remove %s\n", package);
		return 1;
	}

	printf_safe("Removed: %s\n", package);
	return 0;
}
