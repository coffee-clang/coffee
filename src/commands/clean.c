#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_clean(options *opts)
{
	const char *target_dir = opts->target_dir ? opts->target_dir : "target";

	char cmd[4'096];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", target_dir);

	printf("Cleaning %s\n", target_dir);
	if (system(cmd) != 0) {
		fprintf(stderr, "Error: Could not clean target directory\n");
		return 1;
	}

	return 0;
}
