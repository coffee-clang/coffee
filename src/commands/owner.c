#include "../coffee.h"

#include <stdio.h>
#include <string.h>

int64_t handle_owner(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: Missing owner subcommand. Supported: add, remove, list\n");
		return 1;
	}

	char *sub = opts->inputs[1];
	if (strcmp(sub, "add") != 0 && strcmp(sub, "remove") != 0 && strcmp(sub, "list") != 0) {
		fprintf_safe(stderr, "Error: Unknown owner subcommand '%s'. Supported: add, remove, list\n", sub);
		return 1;
	}

	fprintf_safe(stderr, "Error: 'owner' is not yet supported. Registry write API does not exist.\n");
	return 1;
}
