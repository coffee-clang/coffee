#include "../coffee.h"
#include "../manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int create_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return 0;
    }
    return mkdir(path, 0755);
}

static int create_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }
    fprintf(fp, "%s", content);
    fclose(fp);
    return 0;
}

static char *get_name_from_path(const char *path) {
    const char *name = path;
    char *last_slash = strrchr(path, '/');
    if (last_slash) {
        name = last_slash + 1;
    }
    if (!name || strlen(name) == 0) {
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

int64_t handle_new(options *opts) {
    char *path = NULL;
    
    if (opts->inputs_num > 1) {
        path = opts->inputs[1];
    }
    
    if (!path || strlen(path) == 0) {
        path = ".";
    }
    
    if (strcmp(path, ".") != 0) {
        if (create_dir(path) != 0) {
            fprintf(stderr, "Error: Could not create project directory\n");
            return 1;
        }
    }
    
    char manifest_path[4096];
    snprintf(manifest_path, sizeof(manifest_path), "%s/Coffee.toml", path);
    
    if (access(manifest_path, F_OK) == 0) {
        fprintf(stderr, "Error: Project already exists at %s\n", path);
        return 1;
    }
    
    char src_dir[4096];
    snprintf(src_dir, sizeof(src_dir), "%s/src", path);
    
    if (create_dir(src_dir) != 0) {
        fprintf(stderr, "Error: Could not create src directory\n");
        return 1;
    }
    
    char *name = get_name_from_path(path);
    
    char manifest_content[4096];
    snprintf(manifest_content, sizeof(manifest_content),
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
        "headers = [\"include/*.h\"]\n",
        name);
    
    if (create_file(manifest_path, manifest_content) != 0) {
        fprintf(stderr, "Error: Could not create Coffee.toml\n");
        free(name);
        return 1;
    }
    
    char src_main[4096];
    snprintf(src_main, sizeof(src_main), "%s/main.c", src_dir);
    
    char main_content[4096];
    snprintf(main_content, sizeof(main_content),
        "#include <stdio.h>\n"
        "\n"
        "int main(int argc, char **argv) {\n"
        "    printf(\"Hello, world!\\n\");\n"
        "    return 0;\n"
        "}\n");
    
    if (create_file(src_main, main_content) != 0) {
        fprintf(stderr, "Error: Could not create main.c\n");
        free(name);
        return 1;
    }
    
    printf("Created new C project: %s\n", name);
    printf("  - Coffee.toml\n");
    printf("  - src/main.c\n");
    
    free(name);
    return 0;
}
