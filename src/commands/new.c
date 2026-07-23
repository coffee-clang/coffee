#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../skeleton.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

static sds get_name_from_path(const char *path)
{
	const char *name       = path;
	const char *last_slash = strrchr(path, '/');
	if (last_slash) {
		name = last_slash + 1;
	}
	if (name == nullptr || strlen(name) == 0) {
		name = "project";
	}
	sds result = sdsnew(name);
	for (char *p = result; *p; p++) {
		if (*p == '-' || *p == '.') {
			*p = '_';
		}
	}
	return result;
}

int64_t handle_new(options *opts)
{
	char *path = nullptr;

	if (opts->inputs_num > 1) {
		path = opts->inputs[1];
	}

	if (path == nullptr || strlen(path) == 0) {
		path = ".";
	}

	if (strcmp(path, ".") != 0) {
		if (create_dir(path) != 0) {
			fprintf_safe(stderr, "Error: Could not create project directory\n");
			return 1;
		}
	}

	sds manifest_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", path);

	if (access(manifest_path, F_OK) == 0) {
		sdsfree(manifest_path);
		fprintf_safe(stderr, "Error: Project already exists at %s\n", path);
		return 1;
	}

	sds name = get_name_from_path(path);

	/* Create canonical directory structure */
	sds include_dir = sdscatprintf(sdsempty(), "%s/include", path);
	sds src_dir     = sdscatprintf(sdsempty(), "%s/src", path);
	sds deps_dir    = sdscatprintf(sdsempty(), "%s/deps", path);
	sds tests_dir   = sdscatprintf(sdsempty(), "%s/tests", path);
	sds docs_dir    = sdscatprintf(sdsempty(), "%s/docs", path);
	sds scripts_dir = sdscatprintf(sdsempty(), "%s/scripts", path);
	sds build_dir   = sdscatprintf(sdsempty(), "%s/build", path);

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
	sds include_project_dir = sdscatprintf(sdsempty(), "%s/include/%s", path, name);
	if (create_dir(include_project_dir) != 0) {
		sdsfree(include_project_dir);
		fprintf_safe(stderr, "Error: Could not create include/%s directory\n", name);
		sdsfree(name);
		return 1;
	}
	sdsfree(include_project_dir);

	/* Create .gitignore */
	sds gitignore_path    = sdscatprintf(sdsempty(), "%s/.gitignore", path);
	sds gitignore_content = sdsnew(skeleton_gitignore);
	if (create_file(gitignore_path, gitignore_content) != 0) {
		sdsfree(gitignore_path);
		sdsfree(gitignore_content);
		fprintf_safe(stderr, "Error: Could not create .gitignore\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(gitignore_path);
	sdsfree(gitignore_content);

	/* Create LICENSE */
	sds license_path    = sdscatprintf(sdsempty(), "%s/LICENSE", path);
	sds license_content = sdsnew(skeleton_LICENSE);
	if (create_file(license_path, license_content) != 0) {
		sdsfree(license_path);
		sdsfree(license_content);
		fprintf_safe(stderr, "Error: Could not create LICENSE\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(license_path);
	sdsfree(license_content);

	/* Create README.md */
	sds readme_path    = sdscatprintf(sdsempty(), "%s/README.md", path);
	sds readme_content = skeleton_substitute(skeleton_README_md, name);
	if (create_file(readme_path, readme_content) != 0) {
		sdsfree(readme_path);
		sdsfree(readme_content);
		fprintf_safe(stderr, "Error: Could not create README.md\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(readme_path);
	sdsfree(readme_content);

	/* Create Makefile */
	sds makefile_path    = sdscatprintf(sdsempty(), "%s/Makefile", path);
	sds makefile_content = skeleton_substitute(skeleton_Makefile, name);
	if (create_file(makefile_path, makefile_content) != 0) {
		sdsfree(makefile_path);
		sdsfree(makefile_content);
		fprintf_safe(stderr, "Error: Could not create Makefile\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(makefile_path);
	sdsfree(makefile_content);

	/* Create manifest */
	sds manifest_content = skeleton_substitute(skeleton_Coffee_toml, name);

	if (create_file(manifest_path, manifest_content) != 0) {
		sdsfree(manifest_content);
		fprintf_safe(stderr, "Error: Could not create Coffee.toml\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(manifest_content);

	/* Create main.c */
	sds src_main     = sdscatprintf(sdsempty(), "%s/main.c", src_dir);
	sds main_content = sdsnew(skeleton_main_c);

	if (create_file(src_main, main_content) != 0) {
		sdsfree(src_main);
		sdsfree(main_content);
		fprintf_safe(stderr, "Error: Could not create main.c\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(src_main);
	sdsfree(main_content);

	/* Create main header */
	sds main_header_path = sdscatprintf(sdsempty(), "%s/include/%s/%s.h", path, name, name);
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

	if (opts->lib) {
		/* Library mode: lib.c, library Makefile, [lib] manifest */
		sds src_lib     = sdscatprintf(sdsempty(), "%s/lib.c", src_dir);
		sds lib_content = skeleton_substitute(skeleton_lib_c, name);

		if (create_file(src_lib, lib_content) != 0) {
			sdsfree(src_lib);
			sdsfree(lib_content);
			fprintf_safe(stderr, "Error: Could not create lib.c\n");
			sdsfree(name);
			return 1;
		}
		sdsfree(src_lib);
		sdsfree(lib_content);

		/* Library Makefile — builds static library */
		makefile_path    = sdscatprintf(sdsempty(), "%s/Makefile", path);
		makefile_content = skeleton_substitute(skeleton_Makefile_lib, name);

		if (create_file(makefile_path, makefile_content) != 0) {
			sdsfree(makefile_path);
			sdsfree(makefile_content);
			fprintf_safe(stderr, "Error: Could not create Makefile\n");
			sdsfree(name);
			return 1;
		}
		sdsfree(makefile_path);
		sdsfree(makefile_content);

		/* Library manifest with [lib] section */
		manifest_content = skeleton_substitute(skeleton_Coffee_toml_lib, name);

		if (create_file(manifest_path, manifest_content) != 0) {
			sdsfree(manifest_content);
			fprintf_safe(stderr, "Error: Could not create Coffee.toml\n");
			sdsfree(name);
			return 1;
		}
		sdsfree(manifest_content);

		printf_safe("Created new C library: %s\n", name);
		printf_safe("  - Coffee.toml\n");
		printf_safe("  - .gitignore\n");
		printf_safe("  - LICENSE\n");
		printf_safe("  - README.md\n");
		printf_safe("  - Makefile\n");
		printf_safe("  - include/%s/%s.h\n", name, name);
		printf_safe("  - src/lib.c\n");
		printf_safe("  - deps/\n");
		printf_safe("  - tests/\n");
		printf_safe("  - docs/index.md\n");
		printf_safe("  - scripts/\n");
		printf_safe("  - build/\n");

		sdsfree(name);
		sdsfree(manifest_path);
		sdsfree(include_dir);
		sdsfree(src_dir);
		sdsfree(deps_dir);
		sdsfree(tests_dir);
		sdsfree(docs_dir);
		sdsfree(scripts_dir);
		sdsfree(build_dir);
		return 0;
	}

	/* Create placeholder files */
	sds docs_placeholder_path = sdscatprintf(sdsempty(), "%s/docs/index.md", path);
	if (create_file(docs_placeholder_path, skeleton_index_md) != 0) {
		sdsfree(docs_placeholder_path);
		fprintf_safe(stderr, "Error: Could not create docs/index.md\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(docs_placeholder_path);

	printf_safe("Created new C project: %s\n", name);
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
	sdsfree(manifest_path);
	sdsfree(include_dir);
	sdsfree(src_dir);
	sdsfree(deps_dir);
	sdsfree(tests_dir);
	sdsfree(docs_dir);
	sdsfree(scripts_dir);
	sdsfree(build_dir);
	return 0;
}
