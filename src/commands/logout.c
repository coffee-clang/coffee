#include "../coffee.h"

#include <stdio.h>

int64_t handle_logout(options *opts)
{
	(void)opts;
	printf_safe("This project does not use a central registry.\n");
	printf_safe("No credentials to remove.\n");
	return 0;
}
