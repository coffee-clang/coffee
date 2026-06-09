#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_fmt(options *opts)
{
	(void)opts;
	printf_safe("Formatting source code...\n");

	// Use find to get all .c and .h files in src and tests
	// and run clang-format -i on them.
	i64 ret = system("find src tests -name \"*.c\" -o -name \"*.h\" | xargs clang-format -i 2>/dev/null");

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Formatting failed. Please ensure 'clang-format' is installed.\n");
		return 1;
	}

	printf_safe("Formatting complete.\n");
	return 0;
}
