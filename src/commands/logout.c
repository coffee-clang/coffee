#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_logout(options *opts)
{
	const char *home = getenv("HOME");
	if (home == nullptr) {
		fprintf_safe(stderr, "Error: HOME environment variable not set.\n");
		return 1;
	}

	sds cred_path = sdscatprintf(sdsempty(), "%s/.coffee/credentials", home);

	if (access(cred_path, F_OK) != 0) {
		sdsfree(cred_path);
		printf("Not logged in (no credentials found).\n");
		return 0;
	}

	if (unlink(cred_path) == 0) {
		printf("Successfully logged out. Removed credentials from %s\n", cred_path);
	} else {
		fprintf_safe(stderr, "Error: Could not remove credentials from %s\n", cred_path);
		sdsfree(cred_path);
		return 1;
	}

	sdsfree(cred_path);
	return 0;
}
