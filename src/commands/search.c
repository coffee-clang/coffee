#include "../coffee.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_search(options *opts)
{
	char *query = NULL;

	if (opts->inputs_num > 1) {
		query = opts->inputs[1];
	}

	if (!query) {
		query = "";
	}

	printf("Searching for packages matching '%s'...\n\n", query);

	recipe_list_t *list = registry_search(query);

	if (!list || list->count == 0) {
		printf("No packages found.\n");
		return 0;
	}

	printf("%-20s %-10s %s\n", "NAME", "VERSION", "DESCRIPTION");
	printf("%-20s %-10s %s\n", "----", "-------", "-----------");

	for (size_t i = 0; i < list->count; i++) {
		recipe_t   *r	    = &list->recipes[i];
		const char *name    = r->name ? r->name : "-";
		const char *version = r->version ? r->version : "-";
		const char *desc    = r->description ? r->description : "";

		if (strlen(desc) > 50) {
			printf("%-20s %-10s %.50s...\n", name, version, desc);
		} else {
			printf("%-20s %-10s %s\n", name, version, desc);
		}
	}

	printf("\nTotal: %zu packages\n", list->count);

	registry_free_recipes(list);
	return 0;
}
