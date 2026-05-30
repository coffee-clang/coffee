#include "../coffee.h"
#include "../manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

static int create_dir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0) {
		return 0;
	}
	return mkdir(path, 0755);
}

static int create_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");
	if (fp == nullptr) {
		return -1;
	}
	fprintf_safe(fp, "%s", content);
	fclose(fp);
	return 0;
}

static char *get_name_from_path(const char *path)
{
	const char *name	   = path;
	char	   *last_slash = strrchr(path, '/');
	if (last_slash) {
		name = last_slash + 1;
	}
	if (name == nullptr || strlen(name) == 0) {
		name = "project";
	}
	char *result = strdup(name);
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

	char manifest_path[4'096];
	snprintf_safe(manifest_path, sizeof(manifest_path), "%s/Coffee.toml", path);

	if (access(manifest_path, F_OK) == 0) {
		fprintf_safe(stderr, "Error: Project already exists at %s\n", path);
		return 1;
	}

	char *name = get_name_from_path(path);

	/* Create canonical directory structure */
	char include_dir[4'096];
	char src_dir[4'096];
	char deps_dir[4'096];
	char tests_dir[4'096];
	char docs_dir[4'096];
	char scripts_dir[4'096];
	char build_dir[4'096];

	snprintf_safe(include_dir, sizeof(include_dir), "%s/include", path);
	snprintf_safe(src_dir, sizeof(src_dir), "%s/src", path);
	snprintf_safe(deps_dir, sizeof(deps_dir), "%s/deps", path);
	snprintf_safe(tests_dir, sizeof(tests_dir), "%s/tests", path);
	snprintf_safe(docs_dir, sizeof(docs_dir), "%s/docs", path);
	snprintf_safe(scripts_dir, sizeof(scripts_dir), "%s/scripts", path);
	snprintf_safe(build_dir, sizeof(build_dir), "%s/build", path);

	if (create_dir(include_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create include directory\n");
		free(name);
		return 1;
	}

	if (create_dir(src_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create src directory\n");
		free(name);
		return 1;
	}

	if (create_dir(deps_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create deps directory\n");
		free(name);
		return 1;
	}

	if (create_dir(tests_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create tests directory\n");
		free(name);
		return 1;
	}

	if (create_dir(docs_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create docs directory\n");
		free(name);
		return 1;
	}

	if (create_dir(scripts_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create scripts directory\n");
		free(name);
		return 1;
	}

	if (create_dir(build_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create build directory\n");
		free(name);
		return 1;
	}

	/* Create include directory for project headers */
	char include_project_dir[4'096];
	snprintf_safe(include_project_dir, sizeof(include_project_dir), "%s/include/%s", path, name);
	if (create_dir(include_project_dir) != 0) {
		fprintf_safe(stderr, "Error: Could not create include/%s directory\n", name);
		free(name);
		return 1;
	}

	/* Create .gitignore */
	char gitignore_path[4'096];
	snprintf_safe(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", path);
	char gitignore_content[] = "build/\n"
							   "target/\n"
							   "*.o\n"
							   "*.a\n"
							   "*.so\n"
							   ".tidy_stamps/\n";
	if (create_file(gitignore_path, gitignore_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create .gitignore\n");
		free(name);
		return 1;
	}

	/* Create LICENSE */
	char license_path[4'096];
	snprintf_safe(license_path, sizeof(license_path), "%s/LICENSE", path);
	char license_content[] = "MIT License\n"
							 "\n"
							 "Copyright (c) 2024\n"
							 "\n"
							 "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
							 "of this software and associated documentation files (the \"Software\"), to deal\n"
							 "in the Software without restriction, including without limitation the rights\n"
							 "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
							 "copies of the Software, and to permit persons to whom the Software is\n"
							 "furnished to do so, subject to the following conditions:\n"
							 "\n"
							 "The above copyright notice and this permission notice shall be included in all\n"
							 "copies or substantial portions of the Software.\n"
							 "\n"
							 "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
							 "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
							 "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
							 "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
							 "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
							 "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
							 "SOFTWARE.\n";
	if (create_file(license_path, license_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create LICENSE\n");
		free(name);
		return 1;
	}

	/* Create README.md */
	char readme_path[4'096];
	snprintf_safe(readme_path, sizeof(readme_path), "%s/README.md", path);
	char readme_content[4'096];
	snprintf_safe(readme_content, sizeof(readme_content),
				  "# %s\n"
				  "\n"
				  "A modern C project.\n",
				  name);
	if (create_file(readme_path, readme_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create README.md\n");
		free(name);
		return 1;
	}

	/* Create Makefile */
	char makefile_path[4'096];
	snprintf_safe(makefile_path, sizeof(makefile_path), "%s/Makefile", path);
	char makefile_content[4'096];
	snprintf_safe(makefile_content, sizeof(makefile_content),
				  "CC ?= clang\n"
				  "CFLAGS += -std=c23 -O3 -g\n"
				  "CFLAGS += -Wall -Wextra -Wshadow -Wpedantic\n"
				  "CFLAGS += -Wconversion -Wsign-conversion -Wunused\n"
				  "CFLAGS += -Iinclude/%s\n"
				  "\n"
				  "TARGET := build/%s\n"
				  "SOURCES := $(wildcard src/*.c)\n"
				  "\n"
				  ".PHONY: build clean format tidy\n"
				  "\n"
				  "build: $(SOURCES)\n"
				  "\t$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)\n"
				  "\n"
				  "clean:\n"
				  "\trm -rf build/\n"
				  "\n"
				  "format:\n"
				  "\tclang-format -i src/*.c include/%s/*.h\n"
				  "\n"
				  "tidy:\n"
				  "\tclang-tidy src/*.c -- $(CFLAGS)\n",
				  name, name, name);
	if (create_file(makefile_path, makefile_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create Makefile\n");
		free(name);
		return 1;
	}

	/* Create manifest */
	char manifest_content[4'096];
	snprintf_safe(manifest_content, sizeof(manifest_content),
				  "[package]\n"
				  "name = \"%s\"\n"
				  "version = \"0.1.0\"\n"
				  "edition = \"c23\"\n"
				  "description = \"A new C project\"\n"
				  "license = \"MIT\"\n"
				  "\n"
				  "[dependencies]\n"
				  "\n"
				  "[lib]\n"
				  "sources = [\"src/*.c\"]\n"
				  "headers = [\"include/%s/*.h\"]\n",
				  name, name);

	if (create_file(manifest_path, manifest_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create Coffee.toml\n");
		free(name);
		return 1;
	}

	/* Create main.c */
	char src_main[4'096];
	snprintf_safe(src_main, sizeof(src_main), "%s/main.c", src_dir);

	char main_content[4'096];
	snprintf_safe(main_content, sizeof(main_content),
				  "#include <stdio.h>\n"
				  "\n"
				  "int main(int argc, char **argv) {\n"
				  "    printf(\"Hello, world!\\n\");\n"
				  "    return 0;\n"
				  "}\n");

	if (create_file(src_main, main_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create main.c\n");
		free(name);
		return 1;
	}

	/* Create main header */
	char main_header_path[4'096];
	snprintf_safe(main_header_path, sizeof(main_header_path), "%s/include/%s/%s.h", path, name, name);
	char header_content[4'096];
	snprintf_safe(header_content, sizeof(header_content),
				  "#ifndef %s_H\n"
				  "#define %s_H\n"
				  "\n"
				  "// Your declarations here\n"
				  "\n"
				  "#endif // %s_H\n",
				  name, name, name);
	if (create_file(main_header_path, header_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create include/%s/%s.h\n", name, name);
		free(name);
		return 1;
	}

	if (opts->lib) {
		/* Library mode: lib.c, library Makefile, [lib] manifest */
		char src_lib[4096];
		snprintf_safe(src_lib, sizeof(src_lib), "%s/lib.c", src_dir);

		char lib_content[4096];
		snprintf_safe(lib_content, sizeof(lib_content),
					  "#include \"%s/%s.h\"\n"
					  "\n"
					  "int add(int a, int b)\n"
					  "{\n"
					  "    return a + b;\n"
					  "}\n",
					  name, name);

		if (create_file(src_lib, lib_content) != 0) {
			fprintf_safe(stderr, "Error: Could not create lib.c\n");
			free(name);
			return 1;
		}

		/* Library Makefile — builds static library */
		char makefile_content[4096];
		snprintf_safe(makefile_content, sizeof(makefile_content),
					  "CC ?= clang\n"
					  "CFLAGS += -std=c23 -O3 -g\n"
					  "CFLAGS += -Wall -Wextra -Wshadow -Wpedantic\n"
					  "CFLAGS += -Wconversion -Wsign-conversion -Wunused\n"
					  "CFLAGS += -Iinclude/%s\n"
					  "\n"
					  "AR ?= ar\n"
					  "ARFLAGS := rcs\n"
					  "\n"
					  "TARGET := build/lib%s.a\n"
					  "SOURCES := $(wildcard src/*.c)\n"
					  "OBJECTS := $(SOURCES:src/%.c=build/%.o)\n"
					  "\n"
					  ".PHONY: build clean format tidy\n"
					  "\n"
					  "build: $(TARGET)\n"
					  "\n"
					  "$(TARGET): $(OBJECTS)\n"
					  "\t$(AR) $(ARFLAGS) $@ $^\n"
					  "\n"
					  "build/%.o: src/%.c\n"
					  "\t$(CC) $(CFLAGS) -c $< -o $@\n"
					  "\n"
					  "clean:\n"
					  "\trm -rf build/\n"
					  "\n"
					  "format:\n"
					  "\tclang-format -i src/*.c include/%s/*.h\n"
					  "\n"
					  "tidy:\n"
					  "\tclang-tidy src/*.c -- $(CFLAGS)\n",
					  name, name, name);

		if (create_file(makefile_path, makefile_content) != 0) {
			fprintf_safe(stderr, "Error: Could not create Makefile\n");
			free(name);
			return 1;
		}

		/* Library manifest with [lib] section */
		char manifest_content[4096];
		snprintf_safe(manifest_content, sizeof(manifest_content),
					  "[package]\n"
					  "name = \"%s\"\n"
					  "version = \"0.1.0\"\n"
					  "edition = \"c23\"\n"
					  "description = \"A new C project\"\n"
					  "license = \"MIT\"\n"
					  "\n"
					  "[dependencies]\n"
					  "\n"
					  "[lib]\n"
					  "sources = [\"src/*.c\"]\n"
					  "headers = [\"include/%s/*.h\"]\n",
					  name, name);

		if (create_file(manifest_path, manifest_content) != 0) {
			fprintf_safe(stderr, "Error: Could not create Coffee.toml\n");
			free(name);
			return 1;
		}

		printf("Created new C library: %s\n", name);
		printf("  - Coffee.toml\n");
		printf("  - .gitignore\n");
		printf("  - LICENSE\n");
		printf("  - README.md\n");
		printf("  - Makefile\n");
		printf("  - include/%s/%s.h\n", name, name);
		printf("  - src/lib.c\n");
		printf("  - deps/\n");
		printf("  - tests/\n");
		printf("  - docs/index.md\n");
		printf("  - scripts/\n");
		printf("  - build/\n");

		free(name);
		return 0;
	}

	/* Create placeholder files */
	char docs_placeholder_path[4'096];
	snprintf_safe(docs_placeholder_path, sizeof(docs_placeholder_path), "%s/docs/index.md", path);
	if (create_file(docs_placeholder_path, "# Documentation\n") != 0) {
		fprintf_safe(stderr, "Error: Could not create docs/index.md\n");
		free(name);
		return 1;
	}

	printf("Created new C project: %s\n", name);
	printf("  - Coffee.toml\n");
	printf("  - .gitignore\n");
	printf("  - LICENSE\n");
	printf("  - README.md\n");
	printf("  - Makefile\n");
	printf("  - include/%s/%s.h\n", name, name);
	printf("  - src/main.c\n");
	printf("  - deps/\n");
	printf("  - tests/\n");
	printf("  - docs/index.md\n");
	printf("  - scripts/\n");
	printf("  - build/\n");

	free(name);
	return 0;
}
