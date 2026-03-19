#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_fix(options *)
{
	printf("Attempting to automatically fix warnings...\n");

	// Use find to get all .c files in src and tests
	// and run clang-tidy --fix on them.
	int ret = system("find src tests -name \"*.c\" | xargs clang-tidy --fix --quiet -- -Isrc 2>/dev/null");

	if (ret != 0) {
		fprintf(stderr, "Error: Automatic fix failed. Please ensure 'clang-tidy' is installed and your code is "
				"mostly valid.\n");
		return 1;
	}

	printf("Fixes applied successfully where possible.\n");
	return 0;
}
