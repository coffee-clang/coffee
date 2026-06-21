#include "../build.h"
#include "../coffee.h"

#include <stdio.h>
#include <string.h>

int64_t handle_fmt(options *opts)
{
	(void)opts;
	printf_safe("Formatting source code...\n");

	// Use find to get all .c and .h files in src and tests

	char *argv[] = { "find", "src",   "tests",        "(",  "-name", "*.c", "-o",   "-name", "*.h",
		             ")",    "-exec", "clang-format", "-i", "{}",    "+",   nullptr };
	i64   ret    = run_command(argv, RUN_CMD_QUIET);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Formatting failed. Please ensure 'clang-format' is installed.\n");
		return 1;
	}

	printf_safe("Formatting complete.\n");
	return 0;
}
