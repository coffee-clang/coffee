#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_dep_list(const char *input, char ***out_names, size_t *out_count)
{
	*out_names = NULL;
	*out_count = 0;

	if (!input) {
		return;
	}

	const char *p = input;
	while (*p) {
		while (*p == ' ' || *p == ',') {
			p++;
		}
		if (!*p) {
			break;
		}

		const char *start = p;
		while (*p && *p != ',' && *p != ' ') {
			p++;
		}

		size_t len = (size_t)(p - start);
		if (len > 0) {
			const char *slash	 = (const char *)memchr(start, '/', len);
			size_t		name_len = slash ? (size_t)(slash - start) : len;

			*out_names				 = realloc(*out_names, (*out_count + 1) * sizeof(char *));
			(*out_names)[*out_count] = malloc(name_len + 1);
			memcpy((*out_names)[*out_count], start, name_len);
			(*out_names)[*out_count][name_len] = '\0';
			(*out_count)++;
		}
	}
}

static void print_transitive(const char *name, const char *version, const char *prefix, bool is_last, int depth,
							 int max_depth)
{
	if (depth > max_depth) {
		return;
	}

	const char *connector = is_last ? "└── " : "├── ";
	char	   *ver		  = version ? strdup(version) : NULL;
	printf("%s%s%s v%s\n", prefix, connector, name ? name : "?", ver ? ver : "?");
	free(ver);

	if (depth >= max_depth) {
		return;
	}

	recipe_t *recipe = registry_get(name);
	if (!recipe) {
		return;
	}

	char *child_prefix = malloc(strlen(prefix) + strlen(is_last ? "    " : "│   ") + 1);
	sprintf(child_prefix, "%s%s", prefix, is_last ? "    " : "│   ");

	char **dep_names = NULL;
	size_t dep_count = 0;
	parse_dep_list(recipe->dependencies, &dep_names, &dep_count);

	for (size_t i = 0; i < dep_count; i++) {
		print_transitive(dep_names[i], NULL, child_prefix, i == dep_count - 1, depth + 1, max_depth);
	}

	for (size_t i = 0; i < dep_count; i++) {
		free(dep_names[i]);
	}
	free(dep_names);
	free(child_prefix);

	registry_free_recipe(recipe);
}

int64_t handle_tree(options *)
{
	char *manifest_path = project_find_manifest(NULL);
	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (!m) {
		fprintf(stderr, "Error: Could not parse manifest\n");
		return 1;
	}

	const char *name	= m->package.name ? m->package.name : "project";
	const char *version = m->package.version ? m->package.version : "0.1.0";

	printf("%s v%s\n", name, version);

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry		= m->package.dependencies[i];
		char	   *dep_name	= NULL;
		char	   *dep_version = NULL;
		manifest_extract_dep_info(entry, &dep_name, &dep_version);

		if (!dep_name) {
			continue;
		}

		bool is_last = (i == m->package.dependencies_count - 1);
		print_transitive(dep_name, dep_version, "", is_last, 0, 3);

		free(dep_name);
		free(dep_version);
	}

	manifest_free(m);
	return 0;
}
