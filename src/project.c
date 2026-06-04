#include "project.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgen.h>
#include <sds/sds.h>
#include <unistd.h>

static i64 file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

sds project_find_manifest(sds start_dir)
{
	sds dir;

	if (start_dir) {
		dir = sdsnew(start_dir);
	} else {
		char *cwd_buf = getcwd(nullptr, 0);
		if (cwd_buf == nullptr) {
			return nullptr;
		}
		dir = sdsnew(cwd_buf);
		free(cwd_buf);
	}

	while (1) {
		sds path = sdscatprintf(sdsempty(), "%s/Coffee.toml", dir);
		if (file_exists(path)) {
			sdsfree(dir);
			return path;
		}
		sdsfree(path);

		sds   parent     = sdsdup(dir);
		char *parent_dir = dirname(parent);
		if (strcmp(parent_dir, dir) == 0) {
			sdsfree(parent);
			sdsfree(dir);
			return nullptr;
		}

		sdsfree(dir);
		dir = sdsnew(parent_dir);
		sdsfree(parent);
	}
}

manifest_t *project_load_manifest(sds path)
{
	return manifest_parse(path);
}

sds project_get_name(manifest_t *m)
{
	if (m == nullptr || m->package.name == nullptr) {
		return nullptr;
	}
	return sdsnew(m->package.name);
}
