#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_logout(options *)
{
	const char *home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "Error: HOME environment variable not set.\n");
		return 1;
	}

	char cred_path[4'096];
	snprintf_safe(cred_path, sizeof(cred_path), "%s/.coffee/credentials", home);

	if (access(cred_path, F_OK) != 0) {
		printf("Not logged in (no credentials found).\n");
		return 0;
	}

	if (unlink(cred_path) == 0) {
		printf("Successfully logged out. Removed credentials from %s\n", cred_path);
	} else {
		fprintf(stderr, "Error: Could not remove credentials from %s\n", cred_path);
		return 1;
	}

	return 0;
}
