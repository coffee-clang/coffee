#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

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
	char gitignore_content[] = "build/\n"
	                           "target/\n"
	                           "*.o\n"
	                           "*.a\n"
	                           "*.so\n"
	                           ".tidy_stamps/\n";
	if (create_file(".gitignore", gitignore_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create .gitignore\n");
		sdsfree(name);
		return 1;
	}

	/* Create LICENSE */
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
	if (create_file("LICENSE", license_content) != 0) {
		fprintf_safe(stderr, "Error: Could not create LICENSE\n");
		sdsfree(name);
		return 1;
	}

	/* Create README.md */
	sds readme_content = sdscatprintf(sdsempty(),
	                                  "# %s\n"
	                                  "\n"
	                                  "A modern C project.\n",
	                                  name);
	if (create_file("README.md", readme_content) != 0) {
		sdsfree(readme_content);
		fprintf_safe(stderr, "Error: Could not create README.md\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(readme_content);

	/* Create main.c */
	sds main_content = sdsnew("#include <stdio.h>\n"
	                          "\n"
	                          "int main(i64 argc, char **argv) {\n"
	                          "    printf(\"Hello, world!\\n\");\n"
	                          "    return 0;\n"
	                          "}\n");

	if (create_file("src/main.c", main_content) != 0) {
		sdsfree(main_content);
		fprintf_safe(stderr, "Error: Could not create main.c\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(main_content);

	/* Create main header */
	sds main_header_path = sdscatprintf(sdsempty(), "include/%s/%s.h", name, name);
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

	/* Create placeholder files */
	if (create_file("docs/index.md", "# Documentation\n") != 0) {
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
		manifest_content = sdscatprintf(sdsempty(),
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
		                                "headers = [\"include/%s/*.h\"]\n"
		                                "\n"
		                                "[[bin]]\n"
		                                "name = \"%s\"\n"
		                                "src = [\"src/*.c\"]\n",
		                                name, name, name);
	} else {
		manifest_content = sdscatprintf(sdsempty(),
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
	}

	if (create_file(manifest_path, manifest_content) != 0) {
		sdsfree(manifest_content);
		fprintf_safe(stderr, "Error: Could not create Coffee.toml\n");
		sdsfree(name);
		return 1;
	}
	sdsfree(manifest_content);

	printf("Initialized C project: %s\n", name);
	printf("  - Coffee.toml\n");
	printf("  - .gitignore\n");
	printf("  - LICENSE\n");
	printf("  - README.md\n");
	printf("  - include/%s/%s.h\n", name, name);
	printf("  - src/main.c\n");
	printf("  - deps/\n");
	printf("  - tests/\n");
	printf("  - docs/index.md\n");
	printf("  - scripts/\n");
	printf("  - build/\n");

	sdsfree(name);
	return 0;
}
