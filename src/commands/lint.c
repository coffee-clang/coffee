#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_lint(options *)
{
	printf("Linting source code...\n");

	// Use find to get all .c files in src and tests
	// and run clang-tidy on them.
	int ret = system("find src tests -name \"*.c\" | xargs clang-tidy --quiet -- -Isrc 2>/dev/null");

	if (ret != 0) {
		fprintf(stderr, "Error: Linting failed. Please ensure 'clang-tidy' is installed.\n");
		return 1;
	}

	printf("Linting complete.\n");
	return 0;
}
