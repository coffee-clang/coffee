#ifndef BUILD_H_
#define BUILD_H_

#include "coffee_features.h"
#include "manifest.h"

#include <stdbool.h>

typedef struct {
	bool   verbose;
	bool   release;
	bool   debug;
	bool   locked;
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

/*
 * Resolve a dependency directory: check local deps/<name>/, vendor/<name>/,
 * then global ~/.coffee/deps/<name>/.  Returns the path (caller frees) or
 * nullptr if not found.
 */
sds dep_resolve_dir(const char *name);

/*
 * Append compiler flags for a single dependency to the flags string.
 * Returns the number of .c source files found (appended to src_argv).
 * Pass nullptr for src_list/src_count if only flags are needed.
 */
size_t dep_add_flags(const char *dep_dir, const char *dep_name, sds *flags, sds *src_list, size_t *src_count);

/*
 * Extract dependency name from a raw TOML dependency entry string.
 * Handles "name", "name = ...", and "name = { ... }" formats.
 * Returns a new sds with the bare name (caller frees).
 */
sds dep_parse_name(const char *entry);

#endif
