#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_remove(options *opts)
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

	bool found = false;
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] &&
		    strncmp(m->package.dependencies[i], package_name, strlen(package_name)) == 0) {
			found = true;
			sdsfree(m->package.dependencies[i]);
			for (size_t j = i; j < m->package.dependencies_count - 1; j++) {
				m->package.dependencies[j] = m->package.dependencies[j + 1];
			}
			m->package.dependencies_count--;
			m->package.dependencies =
			    safe_realloc(m->package.dependencies, m->package.dependencies_count * sizeof(sds));
			break;
		}
	}

	if (!found) {
		fprintf_safe(stderr, "Error: Dependency %s not found in manifest\n", package_name);
		manifest_free(m);
		sdsfree(manifest_path);
		return 1;
	}

	if (manifest_write(manifest_path, m) != 0) {
		fprintf_safe(stderr, "Error: Could not write manifest at %s\n", manifest_path);
		manifest_free(m);
		sdsfree(manifest_path);
		return 1;
	}

	printf_safe("Removed dependency: %s\n", package_name);

	/* Clean up Makefile section for this dependency */
	char *dir_end = strrchr(manifest_path, '/');
	sds   makefile_path;
	if (dir_end) {
		size_t dir_len = (size_t)(dir_end - manifest_path) + 1;
		makefile_path  = sdscatprintf(sdsempty(), "%.*sMakefile", (int)dir_len, manifest_path);
	} else {
		makefile_path = sdsnew("Makefile");
	}

	FILE *mf = fopen(makefile_path, "r");
	if (mf) {
		sds dep_header = sdscatprintf(sdsempty(), "# Dep: %s", package_name);

		sds content = sdsempty();
		if (content) {
			char   buf[4096];
			size_t n;
			fseek(mf, 0, SEEK_SET);
			while ((n = fread(buf, 1, sizeof(buf), mf)) > 0) {
				content = sdscatlen(content, buf, n);
			}

			char *dep_start = strstr(content, dep_header);
			if (dep_start) {
				char *dep_end = dep_start;
				i64   lines   = 0;
				while (*dep_end != '\0' && lines < 4) {
					if (*dep_end == '\n') {
						lines++;
					}
					dep_end++;
					if (lines >= 3) {
						break;
					}
				}

				size_t before_len = (size_t)(dep_start - content);
				size_t after_len  = sdslen(content) - (size_t)(dep_end - content);

				fclose(mf);
				mf = fopen(makefile_path, "w");
				if (mf) {
					fwrite(content, 1, before_len, mf);
					fwrite(dep_end, 1, after_len, mf);
					fclose(mf);
					printf_safe("  Cleaned up Makefile section for %s\n", package_name);
					mf = nullptr; /* Prevent double-close */
				}
			}
			sdsfree(content);
		}
		sdsfree(dep_header);
		if (mf) {
			fclose(mf);
		}
	}

	sdsfree(makefile_path);
	manifest_free(m);
	sdsfree(manifest_path);
	return 0;
}
