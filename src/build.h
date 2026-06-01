#ifndef BUILD_H_
#define BUILD_H_

#include "coffee_features.h"
#include "manifest.h"

#include <stdbool.h>

typedef struct {
	bool   verbose;
	bool   release;
	bool   debug;
	sds    target;
	sds    target_dir;
	i64    jobs;
	sds   *features;
	size_t features_count;
	bool   all_features;
	bool   no_default_features;
} build_opts_t;

i64 build_project(manifest_t *manifest, build_opts_t *opts);
i64 build_run(manifest_t *manifest, build_opts_t *opts, sds *args, i64 argc);

#endif
