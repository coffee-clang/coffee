#ifndef BUILD_H_
#define BUILD_H_

#include "manifest.h"

#include <stdbool.h>

typedef struct {
	bool  verbose;
	bool  release;
	bool  debug;
	char *target;
	char *target_dir;
	int   jobs;
} build_opts_t;

int build_project(manifest_t *manifest, build_opts_t *opts);
int build_run(manifest_t *manifest, build_opts_t *opts, char **args, int argc);

#endif
