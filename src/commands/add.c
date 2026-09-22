#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

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

	char *package_name = opts->inputs[1];

	/* Validate everything that will be interpolated into Coffee.toml
	 * before touching the manifest.  The name is emitted as a bare TOML
	 * key, so it must be restricted to a charset that cannot terminate
	 * the key or start a new one.  is_safe_package_name() is the check
	 * that enforces that; dep_name_is_valid() is kept alongside it for
	 * parity with the parse-side guard in manifest.c, so relaxing the
	 * charset rule alone cannot reopen the hole. */
	if (!dep_name_is_valid(package_name) || !is_safe_package_name(package_name)) {
		fprintf_safe(stderr, "Error: invalid package name '%s'\n", package_name);
		fprintf_safe(stderr, "  Names must match [A-Za-z0-9_-]+\n");
		return 1;
	}
	/* Validate only the source that is actually emitted: --path takes
	 * precedence over --git, and --pkg-version is written only for a
	 * path dep.  Rejecting a value that would be ignored would be noise. */
	if (opts->path != nullptr) {
		if (!url_is_valid(opts->path)) {
			fprintf_safe(stderr, "Error: invalid path '%s'\n", opts->path);
			return 1;
		}
		if (opts->pkg_version != nullptr && !version_is_valid(opts->pkg_version)) {
			fprintf_safe(stderr, "Error: invalid version '%s'\n", opts->pkg_version);
			return 1;
		}
	} else if (opts->git != nullptr && !url_is_valid(opts->git)) {
		fprintf_safe(stderr, "Error: invalid git URL '%s'\n", opts->git);
		return 1;
	}

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

	/* Check if dependency already exists (exact name match, both arrays) */
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] == nullptr) {
			continue;
		}
		sds  dep_name = dep_parse_name(m->package.dependencies[i]);
		bool match    = strcmp(dep_name, package_name) == 0;
		sdsfree(dep_name);
		if (match) {
			printf_safe("Dependency %s already exists\n", package_name);
			manifest_free(m);
			sdsfree(manifest_path);
			return 0;
		}
	}
	for (size_t i = 0; i < m->dependencies.deps_count; i++) {
		if (m->dependencies.deps[i].name != nullptr && strcmp(m->dependencies.deps[i].name, package_name) == 0) {
			printf_safe("Dependency %s already exists\n", package_name);
			manifest_free(m);
			sdsfree(manifest_path);
			return 0;
		}
	}

	/* Add new dependency.  The no-source check stays here, after the
	 * already-exists check, so that re-adding an existing dep without a
	 * source remains the documented no-op. */
	sds dep_str;
	if (opts->git == nullptr && opts->path == nullptr) {
		fprintf_safe(stderr, "Error: use --git <url> or --path <path> to specify the dependency source\n");
		manifest_free(m);
		sdsfree(manifest_path);
		return 1;
	}
	if (opts->path) {
		sds esc_path = toml_escape(opts->path);
		if (opts->pkg_version) {
			sds esc_version = toml_escape(opts->pkg_version);
			dep_str         = sdscatprintf(sdsempty(), "%s = { path = \"%s\", version = \"%s\" }", package_name,
			                               esc_path, esc_version);
			sdsfree(esc_version);
		} else {
			dep_str = sdscatprintf(sdsempty(), "%s = { path = \"%s\" }", package_name, esc_path);
		}
		sdsfree(esc_path);
	} else {
		sds esc_git = toml_escape(opts->git);
		dep_str     = sdscatprintf(sdsempty(), "%s = { git = \"%s\" }", package_name, esc_git);
		sdsfree(esc_git);
	}

	/* Prefix for dev/build deps */
	if (opts->dev) {
		dep_str = sdscatprintf(dep_str, "  # dev");
	} else if (opts->build_dep) {
		dep_str = sdscatprintf(dep_str, "  # build");
	}

	m->package.dependencies_count++;
	m->package.dependencies = safe_realloc(m->package.dependencies, m->package.dependencies_count * sizeof(sds));
	m->package.dependencies[m->package.dependencies_count - 1] = sdsnew(dep_str);
	sdsfree(dep_str);

	if (manifest_write(manifest_path, m) != 0) {
		fprintf_safe(stderr, "Error: Could not write manifest at %s\n", manifest_path);
		manifest_free(m);
		sdsfree(manifest_path);
		return 1;
	}

	printf_safe("Added dependency: %s\n", package_name);
	printf_safe("Run 'coffee fetch' to fetch the new dependency.\n");

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

	FILE *exist_check = safe_fopen(makefile_path, "r");
	if (exist_check) {
		safe_fclose(exist_check);
		FILE *mf = safe_fopen(makefile_path, "a");
		if (mf) {
			fprintf_safe(mf, "\n# Dep: %s\n", package_name);
			fprintf_safe(mf, "CFLAGS += -Ideps/%s/include\n", package_name);
			fprintf_safe(mf, "LDFLAGS += -Ldeps/%s/lib -l%s\n", package_name, package_name);
			if (safe_fclose(mf) != 0) {
				fprintf_safe(stderr, "Warning: failed to write to Makefile\n");
			}
		}
	}

	sdsfree(makefile_path);
	manifest_free(m);
	sdsfree(manifest_path);
	return 0;
}
