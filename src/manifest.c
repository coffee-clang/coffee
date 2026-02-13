#include "manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *strdup_or_null(const char *s) {
    if (s == NULL) return NULL;
    return strdup(s);
}

static void free_package(package_t *pkg) {
    if (pkg->name) free(pkg->name);
    if (pkg->version) free(pkg->version);
    if (pkg->edition) free(pkg->edition);
    if (pkg->description) free(pkg->description);
    if (pkg->license) free(pkg->license);
    if (pkg->repository) free(pkg->repository);
    if (pkg->authors) free(pkg->authors);
    
    for (size_t i = 0; i < pkg->dependencies_count; i++) {
        free(pkg->dependencies[i]);
    }
    free(pkg->dependencies);
    
    for (size_t i = 0; i < pkg->sources_count; i++) {
        free(pkg->sources[i]);
    }
    free(pkg->sources);
    
    for (size_t i = 0; i < pkg->headers_count; i++) {
        free(pkg->headers[i]);
    }
    free(pkg->headers);
}

static void free_dependency(dependency_t *dep) {
    if (dep->name) free(dep->name);
    if (dep->version) free(dep->version);
    if (dep->path) free(dep->path);
    if (dep->git) free(dep->git);
    if (dep->branch) free(dep->branch);
    if (dep->tag) free(dep->tag);
    if (dep->rev) free(dep->rev);
}

static char *toml_datum_to_string(toml_datum_t datum) {
    if (!datum.ok) return NULL;
    return datum.u.s;
}

manifest_t *manifest_parse(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }
    
    char errbuf[256];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    
    if (!conf) {
        return NULL;
    }
    
    manifest_t *m = calloc(1, sizeof(manifest_t));
    if (!m) {
        toml_free(conf);
        return NULL;
    }
    
    toml_table_t *pkg = toml_table_in(conf, "package");
    if (pkg) {
        toml_datum_t name = toml_string_in(pkg, "name");
        m->package.name = toml_datum_to_string(name);
        
        toml_datum_t version = toml_string_in(pkg, "version");
        m->package.version = toml_datum_to_string(version);
        
        toml_datum_t edition = toml_string_in(pkg, "edition");
        m->package.edition = toml_datum_to_string(edition);
        
        toml_datum_t description = toml_string_in(pkg, "description");
        m->package.description = toml_datum_to_string(description);
        
        toml_datum_t license = toml_string_in(pkg, "license");
        m->package.license = toml_datum_to_string(license);
        
        toml_datum_t repository = toml_string_in(pkg, "repository");
        m->package.repository = toml_datum_to_string(repository);
        
        toml_datum_t authors = toml_string_in(pkg, "authors");
        m->package.authors = toml_datum_to_string(authors);
    }
    
    toml_array_t *deps_arr = toml_array_in(conf, "dependencies");
    if (deps_arr) {
        m->package.dependencies_count = toml_array_nelem(deps_arr);
        m->package.dependencies = calloc(m->package.dependencies_count, sizeof(char *));
        for (size_t i = 0; i < m->package.dependencies_count; i++) {
            toml_datum_t dep = toml_string_at(deps_arr, i);
            m->package.dependencies[i] = toml_datum_to_string(dep);
        }
    }
    
    toml_array_t *sources_arr = toml_array_in(conf, "sources");
    if (sources_arr) {
        m->package.sources_count = toml_array_nelem(sources_arr);
        m->package.sources = calloc(m->package.sources_count, sizeof(char *));
        for (size_t i = 0; i < m->package.sources_count; i++) {
            toml_datum_t src = toml_string_at(sources_arr, i);
            m->package.sources[i] = toml_datum_to_string(src);
        }
    }
    
    toml_array_t *headers_arr = toml_array_in(conf, "headers");
    if (headers_arr) {
        m->package.headers_count = toml_array_nelem(headers_arr);
        m->package.headers = calloc(m->package.headers_count, sizeof(char *));
        for (size_t i = 0; i < m->package.headers_count; i++) {
            toml_datum_t hdr = toml_string_at(headers_arr, i);
            m->package.headers[i] = toml_datum_to_string(hdr);
        }
    }
    
    toml_free(conf);
    return m;
}

void manifest_free(manifest_t *m) {
    if (!m) return;
    free_package(&m->package);
    
    for (size_t i = 0; i < m->dependencies.deps_count; i++) {
        free_dependency(&m->dependencies.deps[i]);
    }
    free(m->dependencies.deps);
    
    free(m);
}

int manifest_write(const char *path, manifest_t *m) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    
    fprintf(fp, "[package]\n");
    if (m->package.name) fprintf(fp, "name = \"%s\"\n", m->package.name);
    if (m->package.version) fprintf(fp, "version = \"%s\"\n", m->package.version);
    if (m->package.edition) fprintf(fp, "edition = \"%s\"\n", m->package.edition);
    if (m->package.description) fprintf(fp, "description = \"%s\"\n", m->package.description);
    if (m->package.license) fprintf(fp, "license = \"%s\"\n", m->package.license);
    if (m->package.repository) fprintf(fp, "repository = \"%s\"\n", m->package.repository);
    if (m->package.authors) fprintf(fp, "authors = \"%s\"\n", m->package.authors);
    
    if (m->package.dependencies_count > 0) {
        fprintf(fp, "\n[dependencies]\n");
        for (size_t i = 0; i < m->package.dependencies_count; i++) {
            if (m->package.dependencies[i]) {
                fprintf(fp, "%s\n", m->package.dependencies[i]);
            }
        }
    }
    
    if (m->package.sources_count > 0) {
        fprintf(fp, "\nsources = [\n");
        for (size_t i = 0; i < m->package.sources_count; i++) {
            if (m->package.sources[i]) {
                fprintf(fp, "  \"%s\",\n", m->package.sources[i]);
            }
        }
        fprintf(fp, "]\n");
    }
    
    if (m->package.headers_count > 0) {
        fprintf(fp, "\nheaders = [\n");
        for (size_t i = 0; i < m->package.headers_count; i++) {
            if (m->package.headers[i]) {
                fprintf(fp, "  \"%s\",\n", m->package.headers[i]);
            }
        }
        fprintf(fp, "]\n");
    }
    
    fclose(fp);
    return 0;
}
