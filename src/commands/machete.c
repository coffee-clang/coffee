#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>

static int file_is_source(const char *name)
{
	size_t len = strlen(name);
	if (len < 2) {
		return 0;
	}

	const char *ext = name + len - 2;
	if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
		return 1;
	}

	if (len >= 4) {
		ext = name + len - 4;
		if (strcmp(ext, ".cpp") == 0) {
			return 1;
		}
	}

	return 0;
}

static int scan_file_for_include(const char *filepath, const char *dep_name)
{
	FILE *fp = fopen(filepath, "r");
	if (!fp) {
		return 0;
	}

	char line[4'096];
	while (fgets(line, sizeof(line), fp)) {
		char *p = line;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (strncmp(p, "#include", 8) != 0) {
			continue;
		}

		if (strstr(p, dep_name)) {
			fclose(fp);
			return 1;
		}
	}

	fclose(fp);
	return 0;
}

static int scan_dir_for_dep(const char *dirpath, const char *dep_name)
{
	DIR *dir = opendir(dirpath);
	if (!dir) {
		return 0;
	}

	int			   found = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}

		char path[4'096];
		snprintf_safe(path, sizeof(path), "%s/%s", dirpath, entry->d_name);

		struct stat st;
		if (stat(path, &st) != 0) {
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			found |= scan_dir_for_dep(path, dep_name);
		} else if (S_ISREG(st.st_mode) && file_is_source(entry->d_name)) {
			found |= scan_file_for_include(path, dep_name);
		}

		if (found) {
			break;
		}
	}

	closedir(dir);
	return found;
}

int64_t handle_machete(options *opts)
{
	(void)opts;

	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (!m) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	if (m->package.dependencies_count == 0) {
		printf("No dependencies declared.\n");
		manifest_free(m);
		return 0;
	}

	int unused_count = 0;

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry = m->package.dependencies[i];

		char *name	 = strdup(entry);
		char *equals = strchr(name, '=');
		if (equals) {
			*equals	  = '\0';
			char *end = equals - 1;
			while (end > name && *end == ' ') {
				*end = '\0';
				end--;
			}
		}

		int used = scan_dir_for_dep(".", name);
		if (!used) {
			if (unused_count == 0) {
				printf("Unused dependencies:\n");
			}
			printf("  %s\n", name);
			unused_count++;
		}

		free(name);
	}

	if (unused_count == 0) {
		printf("No unused dependencies found.\n");
	}

	manifest_free(m);
	return 0;
}
