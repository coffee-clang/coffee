#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_clean(options *opts)
{
	const char *target_dir = opts->target_dir != nullptr ? opts->target_dir : "target";

	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", target_dir);

	printf_safe("Cleaning %s\n", target_dir);
	if (system(cmd) != 0) {
		sdsfree(cmd);
		fprintf_safe(stderr, "Error: Could not clean target directory\n");
		return 1;
	}
	sdsfree(cmd);

	return 0;
}
