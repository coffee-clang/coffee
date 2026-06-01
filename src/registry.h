#ifndef REGISTRY_H_
#define REGISTRY_H_

#include "../deps/sds/sds.h"

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

sds coffee_home_dir(void);

recipe_list_t *registry_search(sds query);
recipe_t      *registry_get(sds name);
i64            registry_fetch(sds name, sds version, sds dest_dir);
void           registry_free_recipes(recipe_list_t *list);
void           registry_free_recipe(recipe_t *r);

version_list_t *registry_get_versions(sds name);
void            registry_free_versions(version_list_t *list);

#endif
