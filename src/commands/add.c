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

	// Check if dependency already exists
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] != nullptr &&
		    strncmp(m->package.dependencies[i], package_name, strlen(package_name)) == 0) {
			printf("Dependency %s already exists\n", package_name);
			manifest_free(m);
			sdsfree(manifest_path);
			return 0;
		}
	}

	// Add new dependency
	sds dep_str;
	if (opts->path) {
		if (opts->pkg_version) {
			dep_str = sdscatprintf(sdsempty(), "%s = { path = \"%s\", version = \"%s\" }", package_name, opts->path,
			                       opts->pkg_version);
		} else {
			dep_str = sdscatprintf(sdsempty(), "%s = { path = \"%s\" }", package_name, opts->path);
		}
	} else if (opts->git) {
		dep_str = sdscatprintf(sdsempty(), "%s = { git = \"%s\" }", package_name, opts->git);
	} else {
		const char *version = opts->pkg_version != nullptr ? opts->pkg_version : "*";

		if (opts->features != nullptr || (i64)opts->optional) {
			dep_str = sdscatprintf(sdsempty(), "%s = { version = \"%s\"", package_name, version);
			if (opts->features) {
				dep_str = sdscatprintf(dep_str, ", features = [\"%s\"]", opts->features);
			}
			if (opts->optional) {
				dep_str = sdscatprintf(dep_str, ", optional = true");
			}
			dep_str = sdscatprintf(dep_str, " }");
		} else {
			dep_str = sdscatprintf(sdsempty(), "%s = \"%s\"", package_name, version);
		}
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

	/* Auto-fetch registry deps only; git/path deps handled by `coffee fetch` */
	if (!opts->git && !opts->path) {
		sds ver = nullptr;
		if (opts->pkg_version != nullptr && strcmp(opts->pkg_version, "*") != 0) {
			ver = sdsnew(opts->pkg_version);
		} else {
			version_list_t *versions = registry_get_versions(package_name);
			if (versions != nullptr && versions->count > 0) {
				ver = sdsnew(versions->versions[0]);
				registry_free_versions(versions);
			}
		}

		if (ver != nullptr) {
			const char *coffee_home = coffee_home_dir();
			sds         global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
			mkdir(global_deps, 0755);
			mkdir("deps", 0755);

			sds cache_path = sdscatprintf(sdsempty(), "%s/%s/%s", global_deps, package_name, ver);
			i64 fetch_ret  = registry_fetch(package_name, ver, cache_path);
			if (fetch_ret == 0) {
				sds         local_link = sdscatprintf(sdsempty(), "deps/%s", package_name);
				struct stat st;
				if (lstat(local_link, &st) == 0) {
					sds rm_cmd = sdscatprintf(sdsempty(), "rm -rf %s", local_link);
					system(rm_cmd);
					sdsfree(rm_cmd);
				}
				symlink(cache_path, local_link);
				sdsfree(local_link);
			}
			sdsfree(global_deps);
			sdsfree(cache_path);
			sdsfree(ver);
		}
	}

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
