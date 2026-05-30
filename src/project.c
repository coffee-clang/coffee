#include "project.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgen.h>
#include <unistd.h>

#define MAX_PATH_LEN 4096

static int file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

sds *project_find_manifest(sds start_dir)
{
	char  cwd[MAX_PATH_LEN];
	char *dir;

	if (start_dir) {
		dir = strdup(start_dir);
	} else {
		if (getcwd(cwd, sizeof(cwd)) == NULL) {
			return NULL;
		}
		dir = strdup(cwd);
	}

	char path[MAX_PATH_LEN];

	while (1) {
		snprintf_safe(path, sizeof(path), "%s/Coffee.toml", dir);
		if (file_exists(path)) {
			free(dir);
			return strdup(path);
		}

		snprintf_safe(path, sizeof(path), "%s/Coffee.toml", dir);
		if (file_exists(path)) {
			free(dir);
			return strdup(path);
		}

		char *parent = dirname(strdup(dir));
		if (strcmp(parent, dir) == 0) {
			free(parent);
			free(dir);
			return NULL;
		}

		free(dir);
		dir = parent;
	}
}

manifest_t *project_load_manifest(sds path)
{
	return manifest_parse(path);
}

sds *project_get_name(manifest_t *m)
{
	if (!m || !m->package.name) {
		return NULL;
	}
	return strdup(m->package.name);
}
