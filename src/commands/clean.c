#include "../build.h"
#include "../coffee.h"

#include <stdio.h>
#include <string.h>

int64_t handle_clean(options *opts)
{
	char *target_dir = opts->target_dir != nullptr ? opts->target_dir : (char *)"build";

	if (target_dir[0] == '/' || strcmp(target_dir, "..") == 0 || strncmp(target_dir, "../", 3) == 0) {
		fprintf_safe(stderr, "Error: refusing to clean unsafe target directory '%s'\n", target_dir);
		return 1;
	}

	printf_safe("Cleaning %s\n", target_dir);
	char *rm_argv[] = { "rm", "-rf", target_dir, nullptr };
	if (run_command(rm_argv, 0) != 0) {
		fprintf_safe(stderr, "Error: Could not clean target directory\n");
		return 1;
	}

	return 0;
}
