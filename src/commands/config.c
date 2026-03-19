#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>

int64_t handle_config(options *)
{
	printf("Coffee configuration:\n\n");
	printf("Environment variables:\n");
	printf("  CC:     %s\n", getenv("CC") ? getenv("CC") : "clang (default)");
	printf("  CFLAGS: %s\n", getenv("CFLAGS") ? getenv("CFLAGS") : "(none)");
	printf("\nSubcommand-based configuration management is not yet implemented.\n");
	return 0;
}
