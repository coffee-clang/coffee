#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

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
	sds gitignore_content = sdsnew("build/\n"
	                               "target/\n"
	                               "*.o\n"
	                               "*.a\n"
	                               "*.so\n"
	                               ".tidy_stamps/\n");
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
	sds license_content = sdsnew("MIT License\n"
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
	                             "SOFTWARE.\n");
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
	sds readme_content = sdscatprintf(sdsempty(),
	                                  "# %s\n"
	                                  "\n"
	                                  "A modern C project.\n",
	                                  name);
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
	sds makefile_content = sdscatprintf(sdsempty(),
	                                    "CC ?= clang\n"
	                                    "CFLAGS += -std=c23 -O3 -g\n"
	                                    "CFLAGS += -Wall -Wextra -Wshadow -Wpedantic\n"
	                                    "CFLAGS += -Wconversion -Wsign-conversion -Wunused\n"
	                                    "CFLAGS += -Iinclude/%s\n"
	                                    "\n"
	                                    "# Dependency flags via coffee\n"
	                                    "CFLAGS += $(shell coffee cflags 2>/dev/null)\n"
	                                    "LDFLAGS += $(shell coffee libs 2>/dev/null)\n"
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
		sdsfree(makefile_path);
		sdsfree(makefile_content);
		fprintf_safe(stderr, "Error: Could not create Makefile\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(makefile_path);
	sdsfree(makefile_content);

	/* Create manifest */
	sds manifest_content = sdscatprintf(sdsempty(),
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
		sdsfree(manifest_content);
		fprintf_safe(stderr, "Error: Could not create Coffee.toml\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(manifest_content);

	/* Create main.c */
	sds src_main     = sdscatprintf(sdsempty(), "%s/main.c", src_dir);
	sds main_content = sdsnew("#include <stdio.h>\n"
	                          "\n"
	                          "int main(i64 argc, char **argv) {\n"
	                          "    printf_safe(\"Hello, world!\\n\");\n"
	                          "    return 0;\n"
	                          "}\n");

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
	sds header_content   = sdscatprintf(sdsempty(),
	                                    "#ifndef %s_H\n"
	                                    "#define %s_H\n"
	                                    "\n"
	                                    "// Your declarations here\n"
	                                    "\n"
	                                    "#endif // %s_H\n",
	                                    name, name, name);
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
		sds lib_content = sdscatprintf(sdsempty(),
		                               "#include \"%s/%s.h\"\n"
		                               "\n"
		                               "i64 add(i64 a, i64 b)\n"
		                               "{\n"
		                               "    return a + b;\n"
		                               "}\n",
		                               name, name);

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
		makefile_content = sdscatprintf(sdsempty(),
		                                "CC ?= clang\n"
		                                "CFLAGS += -std=c23 -O3 -g\n"
		                                "CFLAGS += -Wall -Wextra -Wshadow -Wpedantic\n"
		                                "CFLAGS += -Wconversion -Wsign-conversion -Wunused\n"
		                                "CFLAGS += -Iinclude/%s\n"
		                                "\n"
		                                "# Dependency flags via coffee\n"
		                                "CFLAGS += $(shell coffee cflags 2>/dev/null)\n"
		                                "LDFLAGS += $(shell coffee libs 2>/dev/null)\n"
		                                "\n"
		                                "TARGET := build/lib%s.a\n"
		                                "SOURCES := $(wildcard src/*.c)\n"
		                                "OBJECTS := $(SOURCES:src/%%.c=build/%%.o)\n"
		                                "\n"
		                                ".PHONY: build clean format tidy\n"
		                                "\n"
		                                "build: $(TARGET)\n"
		                                "\n"
		                                "$(TARGET): $(OBJECTS)\n"
		                                "\t$(AR) $(ARFLAGS) $@ $^\n"
		                                "\n"
		                                "build/%%.o: src/%%.c\n"
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
			sdsfree(makefile_path);
			sdsfree(makefile_content);
			fprintf_safe(stderr, "Error: Could not create Makefile\n");
			sdsfree(name);
			return 1;
		}
		sdsfree(makefile_path);
		sdsfree(makefile_content);

		/* Library manifest with [lib] section */
		manifest_content = sdscatprintf(sdsempty(),
		                                "[package]\n"
		                                "name = \"%s\"\n"
		                                "version = \"0.1.0\"\n"
		                                "edition = \"c23\"\n"
		                                "description = \"A new C project\"\n"
		                                "license = \"MIT\"\n"
		                                "\n"
		                                "[lib]\n"
		                                "name = \"%s\"\n"
		                                "src = [\"src/lib.c\"]\n"
		                                "include = [\"include\"]\n",
		                                name, name);

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
		return 0;
	}

	/* Create placeholder files */
	sds docs_placeholder_path = sdscatprintf(sdsempty(), "%s/docs/index.md", path);
	if (create_file(docs_placeholder_path, "# Documentation\n") != 0) {
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
	return 0;
}
