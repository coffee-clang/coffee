#include "../coffee.h"
#include "../manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>

static int create_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");
	if (!fp) {
		return -1;
	}
	fprintf(fp, "%s", content);
	fclose(fp);
	return 0;
}

static char *get_name_from_current_dir(void)
{
	char cwd[1024];
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		return strdup("project");
	}
	char *name   = basename(cwd);
	char *result = strdup(name);
	for (char *p = result; *p; p++) {
		if (*p == '-' || *p == '.') {
			*p = '_';
		}
	}
	return result;
}

int64_t handle_init(options *opts)
{
	char *name = NULL;

	if (opts->inputs_num > 1) {
		name = strdup(opts->inputs[1]);
	} else {
		name = get_name_from_current_dir();
	}

	char manifest_path[] = "Coffee.toml";

	if (access(manifest_path, F_OK) == 0) {
		fprintf(stderr, "Error: Coffee.toml already exists in the current directory\n");
		free(name);
		return 1;
	}

	char manifest_content[4'096];
	snprintf(manifest_content, sizeof(manifest_content),
		 "[package]\n"
		 "name = \"%s\"\n"
		 "version = \"0.1.0\"\n"
		 "edition = \"c23\"\n"
		 "description = \"A new C project\"\n"
		 "license = \"MIT\"\n"
		 "\n"
		 "[dependencies]\n"
		 "\n"
		 "[lib]\n"
		 "sources = [\"src/*.c\"]\n"
		 "headers = [\"include/*.h\"]\n",
		 name);

	if (create_file(manifest_path, manifest_content) != 0) {
		fprintf(stderr, "Error: Could not create Coffee.toml\n");
		free(name);
		return 1;
	}

	printf("Initialized C project: %s\n", name);
	printf("  - Coffee.toml\n");

	free(name);
	return 0;
}
