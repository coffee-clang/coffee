#include "../coffee.h"
#include "../registry.h"

#include <stdio.h>

i64 handle_search(options *opts)
{
	sds query = sdsempty();
	if (opts->inputs_num > 1 && opts->inputs[1] != nullptr) {
		query = sdscpy(query, opts->inputs[1]);
	}

	recipe_list_t *results = registry_search(query);
	sdsfree(query);

	if (results == nullptr || results->count == 0) {
		printf_safe("No packages found.\n");
		if (results != nullptr) {
			registry_free_recipes(results);
		}
		return 0;
	}

	for (size_t i = 0; i < results->count; i++) {
		recipe_t *r = &results->recipes[i];
		printf_safe("%-30s %-12s %s\n", r->name ? r->name : "", r->version ? r->version : "",
		            r->description ? r->description : "");
	}

	registry_free_recipes(results);
	return 0;
}
