#include "../build.h"
#include "../coffee.h"
#include "../registry.h"

#include <stdio.h>
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

	sds pkg_dir = sdscatprintf(sdsempty(), "%s/deps/%s", coffee_home_dir(), package);

	struct stat st;
	if (stat(pkg_dir, &st) != 0) {
		sdsfree(pkg_dir);
		fprintf_safe(stderr, "Error: Package '%s' is not installed\n", package);
		return 1;
	}

	sdsfree(pkg_dir);
	char *rm_argv[] = { "rm", "-rf", pkg_dir, nullptr };
	i64   ret       = run_command(rm_argv, 0);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Failed to remove %s\n", package);
		return 1;
	}

	printf_safe("Removed: %s\n", package);
	return 0;
}
