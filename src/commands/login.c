#include "../coffee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

int64_t handle_login(options *)
{
	char token[1'024];

	printf("Enter your API token for the Coffee registry: ");
	if (scanf("%1023s", token) != 1) {
		fprintf(stderr, "Error reading token.\n");
		return 1;
	}

	const char *home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "Error: HOME environment variable not set.\n");
		return 1;
	}

	char coffee_dir[4'096];
	snprintf(coffee_dir, sizeof(coffee_dir), "%s/.coffee", home);
	mkdir(coffee_dir, 0755);

	char cred_path[4'096];
	snprintf(cred_path, sizeof(cred_path), "%s/credentials", coffee_dir);

	FILE *fp = fopen(cred_path, "w");
	if (!fp) {
		fprintf(stderr, "Error: Could not save credentials to %s\n", cred_path);
		return 1;
	}

	fprintf(fp, "token = \"%s\"\n", token);
	fclose(fp);

	printf("Successfully logged in. Credentials saved to %s\n", cred_path);
	return 0;
}
