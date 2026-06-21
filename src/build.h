#ifndef BUILD_H_
#define BUILD_H_

#include "coffee_features.h"
#include "manifest.h"

#include <stdbool.h>

/* Flags for run_command */
#define RUN_CMD_VERBOSE (1 << 0)
#define RUN_CMD_QUIET   (1 << 1)

/*
 * Run an external command with fork+execvp. Returns exit code.
 * Supports flags: RUN_CMD_VERBOSE (print command), RUN_CMD_QUIET (stderr to /dev/null).
 */
i64 run_command(char **argv, int flags);

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

/*
 * Compile a set of source files into a single binary.
 * src_files: array of .c file paths
 * n: number of source files
 * output: path to output binary
 * cc: compiler to use (e.g. "clang")
 * flags: compiler flags string (space-separated, e.g. "-O0 -g -DFOO")
 * verbose: if true, print the compiler command
 * Returns 0 on success, non-zero on failure.
 */
i64 compile_sources(sds *src_files, size_t n, sds output, char *cc, const char *flags_in, bool verbose);

i64 build_project(manifest_t *manifest, build_opts_t *opts);
i64 build_run(manifest_t *manifest, build_opts_t *opts, sds *args, i64 argc);

/*
 * Resolve a dependency directory: check local deps/<name>/, vendor/<name>/,
 * then global ~/.coffee/deps/<name>/.  Returns the path (caller frees) or
 * nullptr if not found.
 */
sds dep_resolve_dir(const char *name);

/*
 * Resolve a dependency directory with version constraint.
 * When constraint is non-null, searches ~/.coffee/deps/<name>/ for
 * versioned subdirectories and returns the highest satisfying version.
 * Falls back to dep_resolve_dir() for null/empty/"*" constraints or
 * if no versioned match is found.
 */
sds dep_resolve_dir_constraint(const char *name, const char *constraint);

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

size_t count_flag_tokens(const char *flags);

sds split_flags_to_argv(const char *flags, char **argv, size_t start_idx, size_t *end_idx);

#endif
