#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static char *run_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    
    char buf[4096];
    size_t total = 0;
    char *buffer = malloc(1);
    buffer[0] = '\0';
    
    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        char *newbuf = realloc(buffer, total + len + 1);
        if (!newbuf) {
            free(buffer);
            pclose(fp);
            return NULL;
        }
        buffer = newbuf;
        memcpy(buffer + total, buf, len);
        total += len;
        buffer[total] = '\0';
    }
    
    pclose(fp);
    return buffer;
}

static char *fetch_url(const char *url) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "curl -sL \"%s\" 2>/dev/null", url);
    return run_command(cmd);
}

static int extract_json_array(const char *json, char ***names, size_t *count) {
    *names = NULL;
    *count = 0;
    
    if (!json) return -1;
    
    const char *p = json;
    int in_name = 0;
    char *current = NULL;
    
    while (*p) {
        if (strncmp(p, "\"name\"", 6) == 0) {
            in_name = 1;
            p += 6;
            while (*p && *p != ':') p++;
            if (*p == ':') p++;
            while (*p && (*p == ' ' || *p == '\"')) p++;
            if (*p == '\"') {
                p++;
                const char *start = p;
                while (*p && *p != '\"') p++;
                if (p > start) {
                    size_t len = p - start;
                    current = malloc(len + 1);
                    memcpy(current, start, len);
                    current[len] = '\0';
                    
                    char **new_names = realloc(*names, (*count + 1) * sizeof(char *));
                    if (new_names) {
                        *names = new_names;
                        (*names)[*count] = current;
                        (*count)++;
                        current = NULL;
                    }
                }
            }
        }
        p++;
    }
    
    return 0;
}

static int extract_string(const char *json, const char *key, char **out) {
    *out = NULL;
    if (!json || !key) return -1;
    
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
    
    if (*p == '\"') {
        p++;
        const char *start = p;
        while (*p && *p != '\"') p++;
        if (p > start) {
            size_t len = p - start;
            *out = malloc(len + 1);
            memcpy(*out, start, len);
            (*out)[len] = '\0';
            return 0;
        }
    } else if (isdigit(*p) || *p == '-') {
        const char *start = p;
        while (*p && (isdigit(*p) || *p == '.' || *p == '-')) p++;
        if (p > start) {
            size_t len = p - start;
            *out = malloc(len + 1);
            memcpy(*out, start, len);
            (*out)[len] = '\0';
            return 0;
        }
    }
    
    return -1;
}

recipe_list_t *registry_search(const char *query) {
    recipe_list_t *list = calloc(1, sizeof(recipe_list_t));
    if (!list) return NULL;
    
    char *response = fetch_url(REGISTRY_URL "/contents/recipes");
    if (!response) {
        return list;
    }
    
    char **letter_dirs = NULL;
    size_t letter_count = 0;
    extract_json_array(response, &letter_dirs, &letter_count);
    free(response);
    
    if (letter_count == 0) {
        return list;
    }
    
    for (size_t i = 0; i < letter_count; i++) {
        char url[4096];
        snprintf(url, sizeof(url), REGISTRY_URL "/contents/recipes/%s", letter_dirs[i]);
        
        char *packages = fetch_url(url);
        if (!packages) continue;
        
        char **pkg_names = NULL;
        size_t pkg_count = 0;
        extract_json_array(packages, &pkg_names, &pkg_count);
        free(packages);
        
        for (size_t j = 0; j < pkg_count; j++) {
            if (query && strlen(query) > 0) {
                if (strstr(pkg_names[j], query) == NULL && 
                    strstr(query, pkg_names[j]) == NULL) {
                    free(pkg_names[j]);
                    continue;
                }
            }
            
            char meta_url[4096];
            snprintf(meta_url, sizeof(meta_url), 
                REGISTRY_RAW_URL "/recipes/%s/%s/library.toml", 
                letter_dirs[i], pkg_names[j]);
            
            char *meta = fetch_url(meta_url);
            if (meta) {
                recipe_t *r = calloc(1, sizeof(recipe_t));
                if (r) {
                    r->name = pkg_names[j];
                    pkg_names[j] = NULL;
                    
                    extract_string(meta, "title", &r->name);
                    extract_string(meta, "version", &r->version);
                    extract_string(meta, "license", &r->license);
                    extract_string(meta, "description", &r->description);
                    extract_string(meta, "recipe_url", &r->download_url);
                    extract_string(meta, "dependencies", &r->dependencies);
                    
                    recipe_t *new_recipes = realloc(list->recipes, 
                        (list->count + 1) * sizeof(recipe_t));
                    if (new_recipes) {
                        list->recipes = new_recipes;
                        list->recipes[list->count++] = *r;
                        free(r);
                    }
                }
                free(meta);
            }
            
            if (pkg_names[j]) free(pkg_names[j]);
        }
        free(pkg_names);
        free(letter_dirs[i]);
    }
    free(letter_dirs);
    
    return list;
}

recipe_t *registry_get(const char *name) {
    if (!name || strlen(name) == 0) {
        return NULL;
    }
    
    char first = tolower(name[0]);
    char url[4096];
    snprintf(url, sizeof(url), 
        REGISTRY_RAW_URL "/recipes/%c/%s/library.toml", first, name);
    
    char *meta = fetch_url(url);
    if (!meta) {
        return NULL;
    }
    
    recipe_t *r = calloc(1, sizeof(recipe_t));
    if (!r) {
        free(meta);
        return NULL;
    }
    
    r->name = strdup(name);
    extract_string(meta, "title", &r->name);
    extract_string(meta, "version", &r->version);
    extract_string(meta, "license", &r->license);
    extract_string(meta, "description", &r->description);
    extract_string(meta, "recipe_url", &r->download_url);
    extract_string(meta, "dependencies", &r->dependencies);
    
    free(meta);
    return r;
}

int registry_fetch(const char *name, const char *version, const char *dest_dir) {
    if (!name || !dest_dir) {
        return -1;
    }
    
    char first = tolower(name[0]);
    char cmd[4096];
    
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", dest_dir);
    if (system(cmd) != 0) {
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd),
        "curl -sL \"" REGISTRY_RAW_URL "/recipes/%c/%s/library.toml\" -o %s/library.toml",
        first, name, dest_dir);
    if (system(cmd) != 0) {
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd),
        "curl -sL \"" REGISTRY_RAW_URL "/recipes/%c/%s/install.sh\" -o %s/install.sh 2>/dev/null",
        first, name, dest_dir);
    system(cmd);
    
    return 0;
}

void registry_free_recipes(recipe_list_t *list) {
    if (!list) return;
    
    for (size_t i = 0; i < list->count; i++) {
        registry_free_recipe(&list->recipes[i]);
    }
    free(list->recipes);
    free(list);
}

void registry_free_recipe(recipe_t *r) {
    if (!r) return;
    if (r->name) free(r->name);
    if (r->version) free(r->version);
    if (r->license) free(r->license);
    if (r->repo) free(r->repo);
    if (r->description) free(r->description);
    if (r->download_url) free(r->download_url);
    if (r->dependencies) free(r->dependencies);
    free(r);
}
