#include "../cmdline.h"
#include "../coffee.h"

#include <stdio.h>

int64_t handle_help(options *opt)
{
	(void)opt;
	printf("Available commands:\n\n");
	for (i64 i = 0; commands[i].name; i++) {
		if (commands[i].description != nullptr && commands[i].description[0] != '\0') {
			printf("  %-24s %s\n", commands[i].name, commands[i].description);
		}
	}
	return 0;
}
