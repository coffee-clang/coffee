#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

static bool is_safe_package_name(const char *name)
{
	const char *p;

	if (name == nullptr || !*name) {
		return false;
	}

	for (p = name; *p; p++) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' ||
		      *p == '-')) {
			return false;
		}
	}
	return true;
}

int64_t handle_add(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: No package specified\n");
		fprintf_safe(stderr, "Usage: coffee add <package>\n");
		fprintf_safe(stderr, "  Use --git <url> or --path <path> to specify the source\n");
		return 1;
	}

	char *package_name  = opts->inputs[1];
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml in current directory or any parent directory\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse manifest at %s\n", manifest_path);
		sdsfree(manifest_path);
		return 1;
	}

	/* Check if dependency already exists */
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] != nullptr &&
		    strncmp(m->package.dependencies[i], package_name, strlen(package_name)) == 0) {
			printf("Dependency %s already exists\n", package_name);
			manifest_free(m);
			sdsfree(manifest_path);
			return 0;
		}
	}

	/* Add new dependency */
	sds dep_str;
	if (!opts->git && !opts->path) {
		recipe_t *recipe = registry_get(sdsnew(package_name));
		if (recipe == nullptr || recipe->version == nullptr) {
			fprintf_safe(stderr, "Error: Package '%s' not found in registry\n", package_name);
			fprintf_safe(stderr, "Use --git <url> or --path <path> to specify the source\n");
			if (recipe != nullptr) {
				registry_free_recipe(recipe);
			}
			manifest_free(m);
			sdsfree(manifest_path);
			return 1;
		}
		dep_str = sdscatprintf(sdsempty(), "%s = \"%s\"", package_name, recipe->version);
		registry_free_recipe(recipe);
	} else if (opts->path) {
		if (opts->pkg_version) {
			dep_str = sdscatprintf(sdsempty(), "%s = { path = \"%s\", version = \"%s\" }", package_name, opts->path,
			                       opts->pkg_version);
		} else {
			dep_str = sdscatprintf(sdsempty(), "%s = { path = \"%s\" }", package_name, opts->path);
		}
	} else {
		dep_str = sdscatprintf(sdsempty(), "%s = { git = \"%s\" }", package_name, opts->git);
	}

	/* Prefix for dev/build deps */
	if (opts->dev) {
		dep_str = sdscatprintf(dep_str, "  # dev");
	} else if (opts->build_dep) {
		dep_str = sdscatprintf(dep_str, "  # build");
	}

	m->package.dependencies_count++;
	m->package.dependencies = realloc(m->package.dependencies, m->package.dependencies_count * sizeof(sds));
	m->package.dependencies[m->package.dependencies_count - 1] = sdsnew(dep_str);
	sdsfree(dep_str);

	if (manifest_write(manifest_path, m) != 0) {
		fprintf_safe(stderr, "Error: Could not write manifest at %s\n", manifest_path);
		manifest_free(m);
		sdsfree(manifest_path);
		return 1;
	}

	printf("Added dependency: %s\n", package_name);
	printf("Run 'coffee fetch' to fetch the new dependency.\n");

	/* Append dependency flags to Makefile if it exists */
	char  *dir_end = strrchr(manifest_path, '/');
	size_t dir_len;
	sds    makefile_path;
	if (dir_end) {
		dir_len       = (size_t)(dir_end - manifest_path) + 1;
		makefile_path = sdscatprintf(sdsempty(), "%.*sMakefile", (int)dir_len, manifest_path);
	} else {
		makefile_path = sdsnew("Makefile");
	}

	if (!is_safe_package_name(package_name)) {
		sdsfree(makefile_path);
		fprintf_safe(stderr, "Warning: package name contains unsafe characters, skipping Makefile update\n");
	} else {
		FILE *exist_check = fopen(makefile_path, "r");
		if (exist_check) {
			fclose(exist_check);
			FILE *mf = fopen(makefile_path, "a");
			if (mf) {
				fprintf_safe(mf, "\n# Dep: %s\n", package_name);
				fprintf_safe(mf, "CFLAGS += -Ideps/%s/include\n", package_name);
				fprintf_safe(mf, "LDFLAGS += -Ldeps/%s/lib -l%s\n", package_name, package_name);
				if (fclose(mf) != 0) {
					fprintf_safe(stderr, "Warning: failed to write to Makefile\n");
				}
			}
		}
	}

	sdsfree(makefile_path);
	manifest_free(m);
	sdsfree(manifest_path);
	return 0;
}
