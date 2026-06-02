#ifndef LOCKFILE_H_
#define LOCKFILE_H_

#include "coffee.h"

typedef struct {
	sds name;    // dependency name
	sds version; // resolved version (from library.toml or "*")
	sds path;    // resolved filepath (e.g. "deps/toml")
} lockfile_dep_t;

typedef struct {
	i64             version; // lockfile format version (1)
	sds             package_name;
	sds             package_version;
	lockfile_dep_t *deps;
	size_t          deps_count;
} lockfile_t;

/**
 * Parse a Coffee.lock file.
 * Returns nullptr on error.
 * Caller must free with lockfile_free().
 */
lockfile_t *lockfile_parse(sds path);

/**
 * Free a lockfile_t and all its contents.
 */
void lockfile_free(lockfile_t *lf);

/**
 * Write a lockfile to the given path.
 * Returns 0 on success, nonzero on error.
 */
i64 lockfile_write(sds path, lockfile_t *lf);

/**
 * Find a dependency entry in the lockfile by name.
 * Returns nullptr if not found.
 */
lockfile_dep_t *lockfile_find_dep(lockfile_t *lf, const char *name);

#endif /* LOCKFILE_H_ */
