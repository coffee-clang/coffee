#include "../build.h"
#include "../coffee.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <toml.h>
#include <unistd.h>

int64_t handle_update(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	/* Determine which dep to update (nullptr = all) */
	char *target = nullptr;
	if (opts->inputs_num > 1) {
		target = opts->inputs[1];
	}

	printf("Updating dependencies...\n");

	lockfile_t lf;
	memset(&lf, 0, sizeof(lf));
	lf.version = 1;

	if (m->package.name) {
		lf.package_name = sdsnew(m->package.name);
	}
	if (m->package.version) {
		lf.package_version = sdsnew(m->package.version);
	}

	/* Resolve all (or targeted) dependencies */
	if (m->package.dependencies_count > 0) {
		lf.deps = calloc(m->package.dependencies_count, sizeof(lockfile_dep_t));
		if (lf.deps == nullptr) {
			manifest_free(m);
			return 1;
		}

		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			sds dep_name = nullptr;
			sds dep_vers = nullptr;
			manifest_extract_dep_info(m->package.dependencies[i], &dep_name, &dep_vers);

			if (dep_name == nullptr) {
				continue;
			}

			/* If a target is specified and this dep doesn't match, keep old lockfile entry */
			if (target != nullptr && strcmp(dep_name, target) != 0) {
				/* Try to preserve existing lockfile entry */
				lockfile_t *old_lf = lockfile_parse("Coffee.lock");
				if (old_lf != nullptr) {
					lockfile_dep_t *old = lockfile_find_dep(old_lf, dep_name);
					if (old != nullptr && old->path != nullptr) {
						lf.deps[lf.deps_count].name    = sdsnew(dep_name);
						lf.deps[lf.deps_count].version = old->version ? sdsnew(old->version) : sdsnew("*");
						lf.deps[lf.deps_count].path    = sdsnew(old->path);
						lf.deps_count++;
						lockfile_free(old_lf);
						sdsfree(dep_name);
						sdsfree(dep_vers);
						continue;
					}
					lockfile_free(old_lf);
				}
				/* No old entry; fall through to resolve */
			}

			printf("  Resolving: %s\n", dep_name);

			sds dep_dir = dep_resolve_dir(dep_name);
			if (dep_dir != nullptr) {
				lf.deps[lf.deps_count].name    = sdsnew(dep_name);
				lf.deps[lf.deps_count].version = resolve_dep_version(dep_dir);
				lf.deps[lf.deps_count].path    = sdsnew(dep_dir);
				sdsfree(dep_dir);
				printf("    Resolved to: %s\n", lf.deps[lf.deps_count].path);
			} else {
				lf.deps[lf.deps_count].name    = sdsnew(dep_name);
				lf.deps[lf.deps_count].version = dep_vers != nullptr ? sdsnew(dep_vers) : sdsnew("*");
				lf.deps[lf.deps_count].path    = sdsnew("");
				fprintf_safe(stderr, "  Warning: %s not found in local deps\n", dep_name);
			}

			lf.deps_count++;
			sdsfree(dep_name);
			sdsfree(dep_vers);
		}
	}

	i64 ret = lockfile_write("Coffee.lock", &lf);

	/* Cleanup */
	sdsfree(lf.package_name);
	sdsfree(lf.package_version);
	for (size_t i = 0; i < lf.deps_count; i++) {
		sdsfree(lf.deps[i].name);
		sdsfree(lf.deps[i].version);
		sdsfree(lf.deps[i].path);
	}
	free(lf.deps);
	manifest_free(m);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Could not write Coffee.lock\n");
		return 1;
	}

	printf("Lockfile updated.\n");
	return 0;
}
