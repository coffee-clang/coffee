#include "../coffee.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_outdated(options *opts)
{
	(void)opts;

	/* Find and parse lockfile */
	sds lockfile_path = project_find_manifest(nullptr);
	if (lockfile_path == nullptr) {
		fprintf_safe(stderr, "Error: No manifest found in current directory\n");
		return 1;
	}

	/* Coffee.lock sits next to the manifest */
	sds    lock_path = sdscatfmt(sdsnew(lockfile_path), "%s", "");
	size_t dir_end   = sdslen(lock_path);
	while (dir_end > 0 && lock_path[dir_end - 1] != '/') {
		dir_end--;
	}
	sdssetlen(lock_path, dir_end);
	lock_path = sdscat(lock_path, "Coffee.lock");
	sdsfree(lockfile_path);

	lockfile_t *lf = lockfile_parse(lock_path);
	if (lf == nullptr) {
		fprintf_safe(stderr, "No lockfile found at Coffee.lock\n");
		fprintf_safe(stderr, "Run 'coffee generate-lockfile' first\n");
		sdsfree(lock_path);
		return 0;
	}

	if (lf->deps_count == 0) {
		printf("No dependencies in lockfile.\n");
		lockfile_free(lf);
		sdsfree(lock_path);
		return 0;
	}

	printf("%-20s %-15s %-15s %s\n", "PACKAGE", "LOCKED", "LATEST", "STATUS");
	printf("%-20s %-15s %-15s %s\n", "-------", "------", "------", "------");

	i64 outdated_count = 0;
	for (size_t i = 0; i < lf->deps_count; i++) {
		const char *pkg    = lf->deps[i].name ? lf->deps[i].name : "(unknown)";
		const char *locked = lf->deps[i].version ? lf->deps[i].version : "*";

		/* Query registry for the latest version */
		recipe_t   *recipe = registry_get(lf->deps[i].name);
		const char *latest = nullptr;
		if (recipe != nullptr && recipe->version != nullptr) {
			latest = recipe->version;
		}

		const char *status;
		if (latest == nullptr) {
			status = "? (not found)";
		} else if (strcmp(locked, latest) == 0) {
			status = "Up-to-date";
		} else {
			status = "Outdated";
			outdated_count++;
		}

		printf("%-20s %-15s %-15s %s\n", pkg, locked, latest ? latest : "?", status);
		registry_free_recipe(recipe);
	}

	printf("\n");
	if (outdated_count > 0) {
		printf("%lld %s outdated.\n", (long long)outdated_count,
		       outdated_count == 1 ? "dependency is" : "dependencies are");
	} else {
		printf("All dependencies are up-to-date.\n");
	}

	lockfile_free(lf);
	sdsfree(lock_path);
	return outdated_count > 0 ? 1 : 0;
}
