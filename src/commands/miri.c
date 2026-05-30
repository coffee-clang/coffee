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

static int analyze_file(const char *filepath, int *file_count, int *issue_count)
{
	FILE *fp = fopen(filepath, "r");
	if (fp == nullptr) {
		return 0;
	}

	(*file_count)++;

	char line[4'096];
	int	 line_num = 0;
	while (fgets(line, sizeof(line), fp)) {
		line_num++;
		char *p = line;
		while (*p == ' ' || *p == '\t') {
			p++;
		}

		if (strncmp(p, "#include", 8) == 0) {
			char *start = strchr(p, '"');
			if (start) {
				start++;
				char *end = strchr(start, '"');
				if (end) {
					*end	  = '\0';
					FILE *inc = fopen(start, "r");
					if (inc == nullptr) {
						printf("  %s:%d: warning: included file '%s' not found\n", filepath, line_num, start);
						(*issue_count)++;
					} else {
						fclose(inc);
					}
				}
			}
		}
	}

	fclose(fp);
	return 1;
}

static int scan_directory(const char *dirpath, int *file_count, int *issue_count)
{
	DIR *dir = opendir(dirpath);
	if (dir == nullptr) {
		return 0;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
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
			scan_directory(path, file_count, issue_count);
		} else if (S_ISREG(st.st_mode) && file_is_source(entry->d_name)) {
			analyze_file(path, file_count, issue_count);
		}
	}

	closedir(dir);
	return 1;
}

int64_t handle_miri(options *opts)
{
	(void)opts;

	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	manifest_free(m);

	int file_count	= 0;
	int issue_count = 0;

	printf("Running static analysis...\n\n");

	scan_directory(".", &file_count, &issue_count);

	if (file_count == 0) {
		fprintf_safe(stderr, "Error: No source files found.\n");
		return 1;
	}

	printf("\nAnalyzed %d file(s), %d issue(s) found.\n", file_count, issue_count);
	return 0;
}
