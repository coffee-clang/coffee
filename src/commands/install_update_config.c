#include "../coffee.h"

#include <stdio.h>

int64_t handle_install_update_config(options *opts)
{
	(void)opts;

	fprintf_safe(stderr, "Error: 'install-update-config' is not yet supported.\n");
	return 1;
}
