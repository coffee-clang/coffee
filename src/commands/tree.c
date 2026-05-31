#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_dep_list(const char *input, char ***out_names, size_t *out_count)
{
	*out_names = nullptr;
	*out_count = 0;

	if (input == nullptr) {
		return;
	}

	const char *p = input;
	while (*p != '\0') {
		while (*p == ' ' || *p == ',') {
			p++;
		}
		if (*p == '\0') {
			break;
		}

		const char *start = p;
		while (*p != '\0' && *p != ',' && *p != ' ') {
			p++;
		}

		size_t len = (size_t)(p - start);
		if (len > 0) {
			const char *slash    = (const char *)memchr(start, '/', len);
			size_t      name_len = slash != nullptr ? (size_t)(slash - start) : len;

			*out_names               = realloc(*out_names, (*out_count + 1) * sizeof(sds));
			(*out_names)[*out_count] = sdsnewlen(start, name_len);
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

	const char *connector = (int)is_last ? "└── " : "├── ";
	sds         ver       = version != nullptr ? sdsnew(version) : nullptr;
	printf("%s%s%s v%s\n", prefix, connector, name != nullptr ? name : "?", ver != nullptr ? ver : "?");
	sdsfree(ver);

	if (depth >= max_depth) {
		return;
	}

	recipe_t *recipe = registry_get(name);
	if (recipe == nullptr) {
		return;
	}

	sds child_prefix = sdscatfmt(sdsnew(prefix), "%s", (int)is_last ? "    " : "│   ");

	char **dep_names = nullptr;
	size_t dep_count = 0;
	parse_dep_list(recipe->dependencies, &dep_names, &dep_count);

	for (size_t i = 0; i < dep_count; i++) {
		print_transitive(dep_names[i], nullptr, child_prefix, i == dep_count - 1, depth + 1, max_depth);
	}

	for (size_t i = 0; i < dep_count; i++) {
		sdsfree(dep_names[i]);
	}
	free(dep_names);
	sdsfree(child_prefix);

	registry_free_recipe(recipe);
}

int64_t handle_tree(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse manifest\n");
		return 1;
	}

	const char *name    = m->package.name != nullptr ? m->package.name : "project";
	const char *version = m->package.version != nullptr ? m->package.version : "0.1.0";

	printf("%s v%s\n", name, version);

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry       = m->package.dependencies[i];
		sds         dep_name    = nullptr;
		sds         dep_version = nullptr;
		manifest_extract_dep_info(entry, &dep_name, &dep_version);

		if (dep_name == nullptr) {
			continue;
		}

		bool is_last = (i == m->package.dependencies_count - 1);
		print_transitive(dep_name, dep_version, "", is_last, 0, 3);

		sdsfree(dep_name);
		sdsfree(dep_version);
	}

	manifest_free(m);
	return 0;
}
