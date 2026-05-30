#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_remove(options *opts)
{
	if (opts->inputs_num < 2) {
		fprintf_safe(stderr, "Error: No package specified\n");
		return 1;
	}

	char *package_name	= opts->inputs[1];
	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml in current directory or any parent directory\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	if (!m) {
		fprintf_safe(stderr, "Error: Could not parse manifest at %s\n", manifest_path);
		free(manifest_path);
		return 1;
	}

	bool found = false;
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		if (m->package.dependencies[i] &&
			strncmp(m->package.dependencies[i], package_name, strlen(package_name)) == 0) {
			found = true;
			free(m->package.dependencies[i]);
			for (size_t j = i; j < m->package.dependencies_count - 1; j++) {
				m->package.dependencies[j] = m->package.dependencies[j + 1];
			}
			m->package.dependencies_count--;
			m->package.dependencies = realloc(m->package.dependencies, m->package.dependencies_count * sizeof(char *));
			break;
		}
	}

	if (!found) {
		fprintf_safe(stderr, "Error: Dependency %s not found in manifest\n", package_name);
		manifest_free(m);
		free(manifest_path);
		return 1;
	}

	if (manifest_write(manifest_path, m) != 0) {
		fprintf_safe(stderr, "Error: Could not write manifest at %s\n", manifest_path);
		manifest_free(m);
		free(manifest_path);
		return 1;
	}

	printf("Removed dependency: %s\n", package_name);

	/* Clean up Makefile section for this dependency */
	char *dir_end = strrchr(manifest_path, '/');
	char  makefile_path[4096];
	if (dir_end) {
		size_t dir_len = (size_t)(dir_end - manifest_path) + 1;
		snprintf_safe(makefile_path, sizeof(makefile_path), "%.*sMakefile", (int)dir_len, manifest_path);
	} else {
		snprintf_safe(makefile_path, sizeof(makefile_path), "Makefile");
	}

	FILE *mf = fopen(makefile_path, "r");
	if (mf) {
		char dep_header[64];
		snprintf_safe(dep_header, sizeof(dep_header), "# Dep: %s", package_name);

		fseek(mf, 0, SEEK_END);
		long mf_len = ftell(mf);
		fseek(mf, 0, SEEK_SET);

		char *content = malloc((size_t)mf_len + 1);
		if (content) {
			size_t read_len	  = fread(content, 1, (size_t)mf_len, mf);
			content[read_len] = '\0';

			char *dep_start = strstr(content, dep_header);
			if (dep_start) {
				char *dep_end = dep_start;
				int	  lines	  = 0;
				while (*dep_end && lines < 4) {
					if (*dep_end == '\n') {
						lines++;
					}
					dep_end++;
					if (lines >= 3) {
						break;
					}
				}

				size_t before_len = (size_t)(dep_start - content);
				size_t after_len  = read_len - (size_t)(dep_end - content);

				fclose(mf);
				mf = fopen(makefile_path, "w");
				if (mf) {
					fwrite(content, 1, before_len, mf);
					fwrite(dep_end, 1, after_len, mf);
					fclose(mf);
					printf("  Cleaned up Makefile section for %s\n", package_name);
					mf = NULL; /* Prevent double-close */
				}
			}
			free(content);
		}
		if (mf) {
			fclose(mf);
		}
	}

	manifest_free(m);
	free(manifest_path);
	return 0;
}
