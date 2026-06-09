#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_grep(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: Pattern to search for not specified.\n");
		return 1;
	}

	char *pattern = opts->inputs[1];
	printf_safe("Searching for '%s'...\n", pattern);

	sds cmd = sdscatprintf(sdsempty(), "grep -rn --exclude-dir=target --exclude-dir=.git \"%s\" src tests", pattern);

	i64 ret = system(cmd);
	sdsfree(cmd);

	if (ret != 0) {
		printf_safe("Pattern not found.\n");
	}

	return 0;
}
