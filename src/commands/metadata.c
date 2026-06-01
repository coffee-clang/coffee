#include "../coffee.h"
#include "../coffee_features.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_transitive_json(const char *name, const char *version, i64 depth, i64 max_depth)
{
	if (depth > max_depth || name == nullptr) {
		return;
	}

	sds ver = version != nullptr ? sdsnew(version) : nullptr;
	printf("{\n");
	printf("  \"name\": \"%s\",\n", name);
	printf("  \"version\": \"%s\"", ver != nullptr ? ver : "?");
	sdsfree(ver);

	if (depth < max_depth) {
		recipe_t *recipe = registry_get(name);
		if (recipe) {
			char **dep_names = nullptr;
			size_t dep_count = 0;

			if (recipe->dependencies) {
				const char *p = recipe->dependencies;
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
						dep_names            = realloc(dep_names, (dep_count + 1) * sizeof(sds));
						dep_names[dep_count] = sdsnewlen(start, name_len);
						dep_count++;
					}
				}
			}

			if (dep_count > 0) {
				printf(",\n  \"dependencies\": [\n");
				for (size_t i = 0; i < dep_count; i++) {
					printf("    ");
					print_transitive_json(dep_names[i], nullptr, depth + 1, max_depth);
					if (i < dep_count - 1) {
						printf(",");
					}
					printf("\n");
				}
				printf("  ]");
			}

			for (size_t i = 0; i < dep_count; i++) {
				sdsfree(dep_names[i]);
			}
			free(dep_names);
			registry_free_recipe(recipe);
		}
	}

	printf("\n}");
}

int64_t handle_metadata(options *opts)
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

	printf("{\n");
	printf("  \"package\": {\n");
	printf("    \"name\": \"%s\",\n", m->package.name != nullptr ? m->package.name : "");
	printf("    \"version\": \"%s\",\n", m->package.version != nullptr ? m->package.version : "");
	printf("    \"edition\": \"%s\",\n", m->package.edition != nullptr ? m->package.edition : "");
	printf("    \"description\": \"%s\",\n", m->package.description != nullptr ? m->package.description : "");
	printf("    \"license\": \"%s\"\n", m->package.license != nullptr ? m->package.license : "");
	printf("  },\n");

	/* Resolved features */
	resolved_features_t *rf = nullptr;
	if (m->features_count > 0) {
		rf = features_resolve(m, nullptr, 0, false, false);
	}
	if (rf != nullptr && rf->package_count > 0) {
		printf("  \"features\": {\n");
		for (size_t i = 0; i < rf->package_count; i++) {
			if (rf->package_names[i] == nullptr) {
				continue;
			}
			printf("    \"%s\": [", rf->package_names[i]);
			for (size_t j = 0; j < rf->packages[i].count; j++) {
				if (rf->packages[i].names[j]) {
					printf("\"%s\"%s", rf->packages[i].names[j], (j < rf->packages[i].count - 1) ? ", " : "");
				}
			}
			printf("]%s\n", (i < rf->package_count - 1) ? "," : "");
		}
		printf("  },\n");
		features_free(rf);
	}

	/* Dependencies */
	printf("  \"dependencies\": [\n");
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		printf("    \"%s\"%s\n", m->package.dependencies[i], (i < m->package.dependencies_count - 1) ? "," : "");
	}
	printf("  ]");

	/* Transitive dependency tree */
	if (m->package.dependencies_count > 0) {
		printf(",\n  \"transitive_deps\": [\n");
		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			const char *entry    = m->package.dependencies[i];
			sds         dep_name = nullptr;
			sds         dep_ver  = nullptr;
			manifest_extract_dep_info(entry, &dep_name, &dep_ver);

			if (dep_name) {
				printf("    ");
				print_transitive_json(dep_name, dep_ver, 0, 3);
				if (i < m->package.dependencies_count - 1) {
					printf(",");
				}
				printf("\n");
				sdsfree(dep_name);
				sdsfree(dep_ver);
			}
		}
		printf("  ]\n");
	} else {
		printf("\n");
	}

	printf("}\n");

	manifest_free(m);
	return 0;
}
