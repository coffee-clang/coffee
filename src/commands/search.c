#include "../coffee.h"
#include "../registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int64_t handle_search(options *opts) {
    char *query = NULL;
    
    if (opts->inputs_num > 1) {
        query = opts->inputs[1];
    }
    
    if (!query) {
        query = "";
    }
    
    printf("Searching for packages matching '%s'...\n\n", query);
    
    char cmd[8192];
    if (strlen(query) == 0) {
        snprintf(cmd, sizeof(cmd),
            "curl -sL \"" REGISTRY_URL "/contents/recipes\" | "
            "grep '\"name\"' | cut -d'\"' -f4 | head -20");
    } else {
        snprintf(cmd, sizeof(cmd),
            "for letter in a b c d e f g h i j k l m n o p q r s t u v w x y z; do "
            "curl -sL \"" REGISTRY_URL "/contents/recipes/$letter\" 2>/dev/null | "
            "grep '\"name\"' | cut -d'\"' -f4 | grep -i '%s'; "
            "done", query);
    }
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        printf("No packages found.\n");
        return 0;
    }
    
    char buf[1024];
    int count = 0;
    printf("%-20s %-10s %-10s %s\n", "NAME", "VERSION", "LICENSE", "DESCRIPTION");
    printf("%-20s %-10s %-10s %s\n", "----", "-------", "-------", "-----------");
    
    while (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\n")] = 0;
        if (strlen(buf) == 0) continue;
        
        char first = tolower(buf[0]);
        char meta_url[4096];
        snprintf(meta_url, sizeof(meta_url),
            REGISTRY_RAW_URL "/recipes/%c/%s/library.toml", first, buf);
        
        char fetch_cmd[8192];
        snprintf(fetch_cmd, sizeof(fetch_cmd),
            "curl -sL \"%s\" 2>/dev/null", meta_url);
        
        FILE *meta_fp = popen(fetch_cmd, "r");
        if (!meta_fp) continue;
        
        char meta_buf[4096] = {0};
        size_t meta_len = 0;
        char meta_line[1024];
        while (fgets(meta_line, sizeof(meta_line), meta_fp) && meta_len < sizeof(meta_buf) - 1) {
            size_t len = strlen(meta_line);
            memcpy(meta_buf + meta_len, meta_line, len);
            meta_len += len;
        }
        meta_buf[meta_len] = '\0';
        pclose(meta_fp);
        
        char *version = NULL;
        char *license = NULL;
        char *description = NULL;
        
        char *p = meta_buf;
        while (*p) {
            if (strncmp(p, "version", 7) == 0) {
                p = strchr(p, '=');
                if (p) {
                    p++;
                    while (*p && (*p == ' ' || *p == '\"')) p++;
                    char *end = strchr(p, '\"');
                    if (end) {
                        version = malloc(end - p + 1);
                        memcpy(version, p, end - p);
                        version[end - p] = '\0';
                    }
                }
            }
            if (strncmp(p, "license", 8) == 0) {
                p = strchr(p, '=');
                if (p) {
                    p++;
                    while (*p && (*p == ' ' || *p == '\"')) p++;
                    char *end = strchr(p, '\"');
                    if (end) {
                        license = malloc(end - p + 1);
                        memcpy(license, p, end - p);
                        license[end - p] = '\0';
                    }
                }
            }
            if (strncmp(p, "description", 12) == 0) {
                p = strchr(p, '=');
                if (p) {
                    p++;
                    while (*p && (*p == ' ' || *p == '\"')) p++;
                    char *end = strchr(p, '\"');
                    if (end) {
                        description = malloc(end - p + 1);
                        memcpy(description, p, end - p);
                        description[end - p] = '\0';
                    }
                }
            }
            p++;
        }
        
        const char *v = version ? version : "-";
        const char *l = license ? license : "-";
        const char *d = description ? description : "-";
        
        if (strlen(d) > 45) {
            printf("%-20s %-10s %-10s %.45s...\n", buf, v, l, d);
        } else {
            printf("%-20s %-10s %-10s %s\n", buf, v, l, d);
        }
        
        if (version) free(version);
        if (license) free(license);
        if (description) free(description);
        
        count++;
    }
    
    pclose(fp);
    
    if (count == 0) {
        printf("No packages found.\n");
    } else {
        printf("\nTotal: %d packages\n", count);
    }
    
    return 0;
}
