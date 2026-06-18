#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../skeleton.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>

static bool has_main_function(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (fp == nullptr) {
		return false;
	}
	char buf[4096];
	bool found = false;
	while (fgets(buf, sizeof(buf), fp) != nullptr) {
		if (strstr(buf, "int main(") != nullptr) {
			found = true;
			break;
		}
	}
	fclose(fp);
	return found;
}

static sds get_name_from_current_dir(void)
{
	sds cwd = sdsnewlen(nullptr, 1024);
	if (getcwd(cwd, 1024) == nullptr) {
		sdsfree(cwd);
		return sdsnew("project");
	}
	char *name   = basename(cwd);
	sds   result = sdsnew(name);
	sdsfree(cwd);
	for (char *p = result; *p; p++) {
		if (*p == '-' || *p == '.') {
			*p = '_';
		}
	}
	return result;
}

int64_t handle_init(options *opts)
{
	sds name = nullptr;

	if (opts->inputs_num > 1) {
		name = sdsnew(opts->inputs[1]);
	} else {
		name = get_name_from_current_dir();
	}

	char manifest_path[] = "Coffee.toml";

	if (access(manifest_path, F_OK) == 0) {
		fprintf_safe(stderr, "Error: Coffee.toml already exists in the current directory\n");
		sdsfree(name);
		return 1;
	}

	/* Create canonical directory structure */
	char include_dir[] = "include";
	char src_dir[]     = "src";
	char deps_dir[]    = "deps";
	char tests_dir[]   = "tests";
	char docs_dir[]    = "docs";
	char scripts_dir[] = "scripts";
	char build_dir[]   = "build";

	if (create_dir(include_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create include directory\n");
		sdsfree(name);
		return 1;
	}

	if (create_dir(src_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create src directory\n");
		sdsfree(name);
		return 1;
	}

	if (create_dir(deps_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create deps directory\n");
		sdsfree(name);
		return 1;
	}

	if (create_dir(tests_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create tests directory\n");
		sdsfree(name);
		return 1;
	}

	if (create_dir(docs_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create docs directory\n");
		sdsfree(name);
		return 1;
	}

	if (create_dir(scripts_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create scripts directory\n");
		sdsfree(name);
		return 1;
	}

	if (create_dir(build_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create build directory\n");
		sdsfree(name);
		return 1;
	}

	/* Create include directory for project headers */
	sds include_project_dir = sdscatprintf(sdsempty(), "include/%s", name);
	if (create_dir(include_project_dir) != 0) {
		sdsfree(include_project_dir);
		fprintf_safe(stderr, "Error: Could not create include/%s directory\n", name);
		sdsfree(name);
		return 1;
	}
	sdsfree(include_project_dir);

	/* Create .gitignore */
	if (create_file(".gitignore", skeleton_gitignore) != 0) {
		fprintf_safe(stderr, "Error: Could not create .gitignore\n");
		sdsfree(name);
		return 1;
	}

	/* Create LICENSE */
	if (create_file("LICENSE", skeleton_LICENSE) != 0) {
		fprintf_safe(stderr, "Error: Could not create LICENSE\n");
		sdsfree(name);
		return 1;
	}

	/* Create README.md */
	sds readme_content = skeleton_substitute(skeleton_README_md, name);
	if (create_file("README.md", readme_content) != 0) {
		sdsfree(readme_content);
		fprintf_safe(stderr, "Error: Could not create README.md\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(readme_content);

	/* Create main.c */
	sds main_content = sdsnew(skeleton_main_c);

	if (create_file("src/main.c", main_content) != 0) {
		sdsfree(main_content);
		fprintf_safe(stderr, "Error: Could not create main.c\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(main_content);

	/* Create Makefile */
	sds makefile_content = skeleton_substitute(skeleton_Makefile, name);
	if (create_file("Makefile", makefile_content) != 0) {
		sdsfree(makefile_content);
		fprintf_safe(stderr, "Error: Could not create Makefile\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(makefile_content);

	/* Create main header */
	sds main_header_path = sdscatprintf(sdsempty(), "include/%s/%s.h", name, name);
	sds header_content   = skeleton_substitute(skeleton_header_h, name);
	if (create_file(main_header_path, header_content) != 0) {
		sdsfree(main_header_path);
		sdsfree(header_content);
		fprintf_safe(stderr, "Error: Could not create include/%s/%s.h\n", name, name);
		sdsfree(name);
		return 1;
	}
	sdsfree(main_header_path);
	sdsfree(header_content);

	/* Create placeholder files */
	if (create_file("docs/index.md", skeleton_index_md) != 0) {
		fprintf_safe(stderr, "Error: Could not create docs/index.md\n");
		sdsfree(name);
		return 1;
	}

	/* Detect if any src file has main() — implies a binary target */
	bool has_bin = false;
	DIR *d       = opendir("src");
	if (d != nullptr) {
		struct dirent *entry;
		sds            src_path = sdsnew("src/");
		size_t         base_len = sdslen(src_path);
		while ((entry = readdir(d)) != nullptr) {
			size_t len = strlen(entry->d_name);
			if (len > 2 && entry->d_name[len - 2] == '.' && entry->d_name[len - 1] == 'c') {
				sdssetlen(src_path, base_len);
				src_path = sdscat(src_path, entry->d_name);
				if (has_main_function(src_path)) {
					has_bin = true;
					sdsfree(src_path);
					break;
				}
			}
		}
		if (!has_bin) {
			sdsfree(src_path);
		}
		closedir(d);
	}

	/* Build manifest content — include [[bin]] if a main() was found */
	sds manifest_content;
	if (has_bin) {
		manifest_content = skeleton_substitute(skeleton_Coffee_toml_bin, name);
	} else {
		manifest_content = skeleton_substitute(skeleton_Coffee_toml, name);
	}

	if (create_file(manifest_path, manifest_content) != 0) {
		sdsfree(manifest_content);
		fprintf_safe(stderr, "Error: Could not create Coffee.toml\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(manifest_content);

	printf_safe("Initialized C project: %s\n", name);
	printf_safe("  - Coffee.toml\n");
	printf_safe("  - .gitignore\n");
	printf_safe("  - LICENSE\n");
	printf_safe("  - README.md\n");
	printf_safe("  - Makefile\n");
	printf_safe("  - include/%s/%s.h\n", name, name);
	printf_safe("  - src/main.c\n");
	printf_safe("  - deps/\n");
	printf_safe("  - tests/\n");
	printf_safe("  - docs/index.md\n");
	printf_safe("  - scripts/\n");
	printf_safe("  - build/\n");

	sdsfree(name);
	return 0;
}
