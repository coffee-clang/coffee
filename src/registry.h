#ifndef REGISTRY_H_
#define REGISTRY_H_

#include <stdbool.h>
#include <stddef.h>

#define REGISTRY_INDEX_URL "https://coffee-clang.github.io/recipes/.well-known/packages.json.zstd"
#define REGISTRY_RAW_URL   "https://raw.githubusercontent.com/coffee-clang/recipes/main"

typedef struct {
	char *name;
	char *version;
	char *license;
	char *repo;
	char *description;
	char *download_url;
	char *dependencies;
} recipe_t;

typedef struct {
	recipe_t *recipes;
	size_t	  count;
} recipe_list_t;

recipe_list_t *registry_search(const char *query);
recipe_t      *registry_get(const char *name);
int	       registry_fetch(const char *name, const char *version, const char *dest_dir);
void	       registry_free_recipes(recipe_list_t *list);
void	       registry_free_recipe(recipe_t *r);

#endif
