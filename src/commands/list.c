#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

int64_t handle_list(options *opts)
{
	/* Check for "installed" subcommand */
	if (opts->inputs_num > 1 && strcmp(opts->inputs[1], "installed") == 0) {
		sds home_dir = sdsnew(coffee_home_dir());
		sds bin_dir  = sdscatprintf(sdsempty(), "%s/bin", home_dir);
		sdsfree(home_dir);

		struct stat st;
		if (stat(bin_dir, &st) != 0) {
			printf_safe("No installed binaries.\n");
			sdsfree(bin_dir);
			return 0;
		}

		DIR *dir = opendir(bin_dir);
		if (dir == nullptr) {
			printf_safe("No installed binaries.\n");
			sdsfree(bin_dir);
			return 0;
		}

		printf_safe("Installed binaries:\n");
		i64            count = 0;
		struct dirent *entry;
		while ((entry = readdir(dir)) != nullptr) {
			if (entry->d_name[0] == '.') {
				continue;
			}
			sds full_path = sdscatprintf(sdsempty(), "%s/%s", bin_dir, entry->d_name);
			if (access(full_path, X_OK) == 0) {
				printf_safe("  %s\n", entry->d_name);
				count++;
			}
			sdsfree(full_path);
		}
		closedir(dir);
		sdsfree(bin_dir);

		if (count == 0) {
			printf_safe("  (none)\n");
		}
		return 0;
	}

	/* Default: list packages in current project */
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: No manifest found\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Failed to parse manifest\n");
		sdsfree(manifest_path);
		return 1;
	}

	printf_safe("Package: %s\n", m->package.name ? m->package.name : "(unnamed)");
	printf_safe("Dependencies:\n");
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i]) {
			printf_safe("  %s\n", m->package.dependencies[i]);
		}
	}

	if (m->bin_count > 0) {
		printf_safe("Binaries:\n");
		for (size_t i = 0; i < m->bin_count; i++) {
			if (m->bin[i].name) {
				printf_safe("  %s\n", m->bin[i].name);
			}
		}
	}

	manifest_free(m);
	sdsfree(manifest_path);
	return 0;
}
