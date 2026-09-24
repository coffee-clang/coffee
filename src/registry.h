#ifndef REGISTRY_H_
#define REGISTRY_H_

/* sds.h is vendored and not lint-clean; suppress its warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-int-conversion"
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#include <sds/sds.h>
#pragma GCC diagnostic pop

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Type aliases used throughout the project */
typedef uint64_t u64;
typedef int64_t  i64;

#define REGISTRY_INDEX_URL "https://coffee-clang.github.io/recipes/.well-known/packages.json.zstd"
#define REGISTRY_RAW_URL   "https://raw.githubusercontent.com/coffee-clang/recipes/main"

typedef struct {
	sds name;
	sds version;
	sds license;
	sds repo;
	sds description;
	sds download_url;
	sds dependencies;
} recipe_t;

typedef struct {
	recipe_t *recipes;
	size_t    count;
} recipe_list_t;

typedef struct {
	sds   *versions;
	size_t count;
} version_list_t;

/*
 * The coffee home directory (~/.coffee or $COFFEE_HOME).  The returned
 * pointer is borrowed from a static buffer — never sdsfree() it.
 */
const char *coffee_home_dir(void);

recipe_list_t *registry_search(sds query);
recipe_t      *registry_get(sds name);
i64            registry_fetch(sds name, const char *version, sds dest_dir);
void           registry_free_recipes(recipe_list_t *list);
void           registry_free_recipe(recipe_t *r);

/*
 * Resolve the upstream source URL for a package from the registry.
 * Returns a new sds (caller frees) or nullptr when no source URL could be
 * used.  When it returns nullptr and recipe_found is non-null,
 * *recipe_found reports whether the recipe itself was retrieved: false
 * means the registry could not be queried at all (unknown package,
 * unreachable, malformed recipe); true means the recipe exists but has no
 * recipe_url or one that fails url_is_valid_remote().
 */
sds registry_source_url(const char *name, bool *recipe_found);

/*
 * Validate a recipe's source URL before it is used for a git clone.
 * Network schemes (https, git, ssh, git+ssh, git+https, scp-style) are
 * always accepted; local filesystem paths are accepted only when the
 * registry base itself is local (COFFEE_REGISTRY_URL is a file:// URL or
 * a path), so a remote registry cannot point clones at local paths.
 */
bool registry_recipe_url_is_valid(const char *url);

version_list_t *registry_get_versions(sds name);
void            registry_free_versions(version_list_t *list);

#endif
