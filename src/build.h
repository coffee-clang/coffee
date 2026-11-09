#ifndef BUILD_H_
#define BUILD_H_

#include "coffee_features.h"
#include "manifest.h"

#include <stdbool.h>

/* Flags for run_command */
#define RUN_CMD_VERBOSE (1 << 0)
#define RUN_CMD_QUIET   (1 << 1)
#define RUN_CMD_NOOUT   (1 << 2)

/*
 * Cast away const for execvp argv arrays. POSIX execvp takes char *const *
 * even though it never modifies arguments, so we suppress -Wcast-qual here.
 * The inline function isolates the pragma so callers stay clean.
 */
static inline char *unconst(const char *p)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	return (char *)p;
#pragma GCC diagnostic pop
}

/*
 * Run an external command with fork+execvp. Returns exit code.
 * Supports flags: RUN_CMD_VERBOSE (print command), RUN_CMD_QUIET (stderr to /dev/null),
 * RUN_CMD_NOOUT (stdout to /dev/null).
 */
i64 run_command(char **argv, int flags);

/*
 * Run an external command and capture its stdout. Returns an sds string
 * (caller frees with sdsfree) or nullptr on failure.
 */
sds run_command_capture(char **argv, int flags);

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

/*
 * Build a project: generate (or reuse) the Makefile and run make on the
 * requested target.  Shared by `coffee build` and `coffee run`.
 * Returns make's exit code.
 *
 * When exe_path_out is non-null, stores the resolved executable path
 * <project_dir>/<target_dir|build>/<package name> in it (caller frees
 * with sdsfree).  Filled in both generated and legacy mode, before make
 * runs, so `coffee run` can exec the binary relative to the project
 * directory.
 */
i64 build_project(options *opts, const char *target, sds *exe_path_out);

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

/*
 * Validate a dependency name.  The name is used both as a path component
 * and as a TOML bare key, so only [A-Za-z0-9_-] is accepted; nullptr,
 * empty, and any other character ('.', '/', '"', whitespace, ...) are
 * rejected.  Returns true if the name is safe in both roles.
 */
bool dep_name_is_valid(const char *name);

/*
 * Validate a make target name before it is passed to make.  Targets
 * legitimately contain '.' (e.g. hello.o), so this is deliberately looser
 * than dep_name_is_valid(): non-empty, no leading '-' (make would parse it
 * as an option), charset [A-Za-z0-9_.-].  '/' is rejected, so build/hello.o
 * is not a valid target; '..' is accepted, as today.
 */
bool build_target_is_valid(const char *target);

/*
 * Validate a version string for filesystem safety before it is used in
 * a path.  Rejects nullptr, empty, leading '.' or '-', and any
 * character outside [A-Za-z0-9._+~*-] (no '/', '\', '"', whitespace, or
 * shell metacharacters).  Deliberately stricter than version_parse().
 */
bool version_is_valid(const char *version);

/*
 * Validate a git ref (branch/tag/rev) before passing it to git.
 * Rejects anything that could be parsed as a git option or that
 * contains option/shell metacharacters.
 */
bool ref_is_valid(const char *ref);

/*
 * True when a ref looks like a full git object id (revision): at least 7
 * hex characters, uppercase or lowercase.  Revisions must be checked out
 * directly instead of being passed to `clone --branch`, which only
 * accepts branches and tags.
 */
bool ref_is_rev(const char *ref);

/*
 * Lowercase a revision ref (hex object ids are case-insensitive) so
 * callers can compare it against `rev-parse` output, which is always
 * lowercase.  Returns a new sds the caller must free; returns nullptr
 * for nullptr.
 */
sds ref_lowercase(const char *ref);

/*
 * Validate a git clone URL.  Explicit schemes (https, git, ssh,
 * git+ssh, git+https), local filesystem paths, and scp-style
 * user@host:path URLs are accepted; anything that could be parsed as a
 * git option (leading '-') or that contains shell metacharacters is
 * rejected.
 */
bool url_is_valid(const char *url);

/*
 * Validate a URL that came from untrusted data (a registry recipe).
 * Stricter than url_is_valid(): local filesystem paths are rejected, so
 * such a URL can only point at a network location.
 */
bool url_is_valid_remote(const char *url);

size_t count_flag_tokens(const char *flags);

sds split_flags_to_argv(const char *flags, char **argv, size_t start_idx, size_t *end_idx);

/*
 * Append each token of `flags` to argv, starting at start_idx, as a
 * "--extra-arg=<token>" element, the form clang-tidy accepts before the
 * file list.  Returns the index after the last element; the appended
 * elements are sds strings that the caller must sdsfree().
 */
size_t append_clang_tidy_extra_args(const char *flags, char **argv, size_t start_idx);

#endif
