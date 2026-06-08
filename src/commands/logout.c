#include "../coffee.h"

#include <stdio.h>

int64_t handle_logout(options *opts)
{
	(void)opts;
	printf("This project does not use a central registry.\n");
	printf("No credentials to remove.\n");
	return 0;
}
