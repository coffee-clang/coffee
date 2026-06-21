#include "../build.h"
#include "../coffee.h"

#include <stdio.h>
#include <string.h>

int64_t handle_grep(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: Pattern to search for not specified.\n");
		return 1;
	}

	char *pattern     = opts->inputs[1];
	char *grep_argv[] = {
		"grep", "-rn", "--exclude-dir=target", "--exclude-dir=.git", pattern, "src", "tests", nullptr
	};
	printf_safe("Searching for '%s'...\n", pattern);

	i64 ret = run_command(grep_argv, 0);

	if (ret != 0) {
		printf_safe("Pattern not found.\n");
	}

	return 0;
}
