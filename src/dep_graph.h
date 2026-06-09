#ifndef DEP_GRAPH_H_
#define DEP_GRAPH_H_

#include "coffee.h"
#include "lockfile.h"
#include "manifest.h"

/*
 * Transitive dependency graph resolver.
 *
 * Reads the root manifest (Coffee.toml), resolves each dependency to a
 * directory on disk, then recursively resolves each transitive dependency's
 * library.toml or Coffee.toml.  Uses DFS with cycle detection.
 *
 * All strings returned are owned by the graph; callers must not free them.
 */

/* A single node in the dependency graph */
typedef struct {
	sds    name;               /* package name */
	sds    path;               /* resolved filesystem path */
	sds    version;            /* version from library.toml or "*" */
	sds    version_constraint; /* constraint from parent manifest (e.g. ">= 2.0") */
	sds    commit;             /* pinned git commit SHA */
	bool   is_git;
	sds    git_ref; /* branch, tag, or rev to track */
	sds    flags;   /* accumulated compiler flags (-I/-L/-l) */
	sds   *sources; /* source .c files */
	size_t src_count;
	bool   visited; /* for cycle detection during build */
} dep_node_t;

typedef struct dep_graph_s {
	dep_node_t *nodes;
	size_t      count;
	size_t      capacity;
	bool        offline;
} dep_graph_t;

/*
 * Get the name of the node at a given index.
 * Index 0 is always the root package.
 * Returns nullptr for invalid index.
 */
const char *dep_graph_node_name(const dep_graph_t *g, size_t index);

/*
 * Create a dependency graph from the root manifest.
 *
 * If lf is non-null, its paths are used as the primary resolution source.
 * If lf is null, each dep is resolved via dep_resolve_dir().
 * offline=true skips all network operations.
 *
 * Returns nullptr on error (cycle detected beyond warning, parse failure).
 * The graph includes the root package itself as entry 0.
 */
dep_graph_t *dep_graph_create(manifest_t *m, lockfile_t *lf, bool offline);

/*
 * Free the graph and all its owned strings.
 */
void dep_graph_free(dep_graph_t *g);

/*
 * Number of entries (root + all transitive deps).
 */
size_t dep_graph_count(const dep_graph_t *g);

/*
 * All package names in the graph (including root at index 0).
 * Count is returned via *count.
 * The caller must free the returned array with free().
 */
sds *dep_graph_names(const dep_graph_t *g, size_t *count);

/*
 * Full compiler flags string for a given dep (-I, -L, -l flags).
 * Returns empty string if dep not found.
 */
sds dep_graph_flags(const dep_graph_t *g, const char *dep_name);

/*
 * Source files (.c) for a given dep.
 * Returns nullptr if none or dep not found.
 * Count is returned via *count.
 */
const sds *dep_graph_sources(const dep_graph_t *g, const char *dep_name, size_t *count);

/*
 * Resolved filesystem path for a dep.
 * Returns nullptr if not found.
 */
const char *dep_graph_path(const dep_graph_t *g, const char *dep_name);

/*
 * Pinned git commit SHA for a dep (from lockfile or resolved).
 * Returns nullptr if not a git dep.
 */
const char *dep_graph_commit(const dep_graph_t *g, const char *dep_name);

/*
 * True if this dep is a git dependency.
 */
bool dep_graph_is_git(const dep_graph_t *g, const char *dep_name);

/*
 * The git ref (branch/tag/rev) that the dep should track.
 * Returns nullptr if not a git dep or no ref specified.
 */
const char *dep_graph_git_ref(const dep_graph_t *g, const char *dep_name);

/*
 * Compare the pinned commit against the remote tracking ref.
 * Returns number of commits the pinned SHA is behind remote.
 * 0 means up-to-date. -1 on error.
 * behind_by is set to a human-readable string if non-null.
 */
i64 dep_graph_compare_remote(const dep_graph_t *g, const char *dep_name, sds *behind_by);

/*
 * Fetch/update a git dep: git fetch origin, then checkout the configured ref.
 * Returns 0 on success.
 */
i64 dep_graph_fetch_git(dep_graph_t *g, const char *dep_name, bool verbose);

/*
 * Get or create a dependency graph with caching.
 *
 * If project_dir is non-null, loads from cache at
 * <project_dir>/.coffee/build-cache/<package>.graph when valid.
 * A cache is valid when it exists and its stored mtimes for
 * Coffee.toml and Coffee.lock match the current filesystem.
 * On cache miss, falls back to dep_graph_create() and writes the
 * result to cache. If project_dir is null, behaves like
 * dep_graph_create() without caching.
 */
dep_graph_t *dep_graph_get(manifest_t *m, lockfile_t *lf, bool offline, const char *project_dir);

#endif /* DEP_GRAPH_H_ */
