#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_grep(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf(stderr, "Error: Pattern to search for not specified.\n");
		return 1;
	}

	char *pattern = opts->inputs[1];
	printf("Searching for '%s'...\n", pattern);

	char cmd[4'096];
	snprintf(cmd, sizeof(cmd), "grep -rn --exclude-dir=target --exclude-dir=.git \"%s\" src tests", pattern);

	int ret = system(cmd);

	if (ret != 0) {
		printf("Pattern not found.\n");
	}

	return 0;
}
