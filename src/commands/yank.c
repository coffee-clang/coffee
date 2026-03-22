#include "../coffee.h"

#include <stdio.h>
#include <string.h>

int64_t handle_yank(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf(stderr, "Error: No package specified. Usage: coffee yank <package>@<version>\n");
		return 1;
	}

	char *arg = opts->inputs[1];
	if (!strchr(arg, '@')) {
		fprintf(stderr, "Error: Invalid format. Usage: coffee yank <package>@<version>\n");
		return 1;
	}

	fprintf(stderr, "Error: 'yank' is not yet supported. Registry write API does not exist.\n");
	return 1;
}
