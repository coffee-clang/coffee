#include "../cmdline.h"
#include "../coffee.h"

#include <stdio.h>

int64_t handle_version(options *)
{
	printf("coffee %s\n", CMDLINE_PARSER_VERSION);
	return 0;
}
