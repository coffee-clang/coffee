#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
		free(manifest_path);
		return 1;
	}

	// Check if dependency already exists
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] != nullptr &&
		    strncmp(m->package.dependencies[i], package_name, strlen(package_name)) == 0) {
			printf("Dependency %s already exists\n", package_name);
			manifest_free(m);
			free(manifest_path);
			return 0;
		}
	}

	// Add new dependency
	char dep_str[1024];
	if (opts->path) {
		if (opts->pkg_version) {
			snprintf_safe(dep_str, sizeof(dep_str), "%s = { path = \"%s\", version = \"%s\" }", package_name,
			              opts->path, opts->pkg_version);
		} else {
			snprintf_safe(dep_str, sizeof(dep_str), "%s = { path = \"%s\" }", package_name, opts->path);
		}
	} else if (opts->git) {
		snprintf_safe(dep_str, sizeof(dep_str), "%s = { git = \"%s\" }", package_name, opts->git);
	} else {
		const char *version = opts->pkg_version != nullptr ? opts->pkg_version : "*";

		if (opts->features != nullptr || (int)opts->optional) {
			int off = snprintf_safe(dep_str, sizeof(dep_str), "%s = { version = \"%s\"", package_name, version);
			if (opts->features) {
				off += snprintf_safe(dep_str + off, sizeof(dep_str) - off, ", features = [\"%s\"]", opts->features);
			}
			if (opts->optional) {
				off += snprintf_safe(dep_str + off, sizeof(dep_str) - off, ", optional = true");
			}
			snprintf_safe(dep_str + off, sizeof(dep_str) - off, " }");
		} else {
			snprintf_safe(dep_str, sizeof(dep_str), "%s = \"%s\"", package_name, version);
		}
	}

	/* Prefix for dev/build deps */
	if (opts->dev) {
		size_t dep_len = strlen(dep_str);
		snprintf_safe(dep_str + dep_len, sizeof(dep_str) - dep_len, "  # dev");
	} else if (opts->build_dep) {
		size_t dep_len = strlen(dep_str);
		snprintf_safe(dep_str + dep_len, sizeof(dep_str) - dep_len, "  # build");
	}

	m->package.dependencies_count++;
	m->package.dependencies = realloc(m->package.dependencies, m->package.dependencies_count * sizeof(char *));
	m->package.dependencies[m->package.dependencies_count - 1] = strdup(dep_str);

	if (manifest_write(manifest_path, m) != 0) {
		fprintf_safe(stderr, "Error: Could not write manifest at %s\n", manifest_path);
		manifest_free(m);
		free(manifest_path);
		return 1;
	}

	printf("Added dependency: %s\n", package_name);

	/* Append dependency flags to Makefile if it exists */
	char  *dir_end = strrchr(manifest_path, '/');
	size_t dir_len;
	char   makefile_path[4'096];
	if (dir_end) {
		dir_len = (size_t)(dir_end - manifest_path) + 1;
		snprintf_safe(makefile_path, sizeof(makefile_path), "%.*sMakefile", (int)dir_len, manifest_path);
	} else {
		snprintf_safe(makefile_path, sizeof(makefile_path), "Makefile");
	}

	if (!is_safe_package_name(package_name)) {
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

	manifest_free(m);
	free(manifest_path);
	return 0;
}
