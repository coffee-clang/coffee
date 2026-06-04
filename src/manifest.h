#ifndef MANIFEST_H_
#define MANIFEST_H_

#include "coffee.h"

#include <toml.h>

typedef struct {
	sds    name;
	sds    version;
	sds    edition;
	sds    description;
	sds    license;
	sds    repository;
	sds    authors;
	sds   *dependencies;
	size_t dependencies_count;
	sds   *sources;
	size_t sources_count;
	sds   *headers;
	size_t headers_count;
} package_t;

typedef struct {
	package_t package;
	sds       name;
	sds       version;
	sds       path;
	sds       git;
	sds       branch;
	sds       tag;
	sds       rev;
	bool      optional;
} dependency_t;

typedef struct {
	dependency_t *deps;
	size_t        deps_count;
} dependencies_t;

typedef struct {
	sds    name;
	sds   *deps;
	size_t deps_count;
} feature_def_t;

typedef struct {
	sds    name;
	sds   *src; /* source file globs */
	size_t src_count;
} binary_target_t;

typedef struct {
	sds   *sources;
	size_t sources_count;
	sds    harness;
	sds    framework;
} test_section_t;

typedef struct {
	package_t        package;
	dependencies_t   dependencies;
	feature_def_t   *features;
	size_t           features_count;
	binary_target_t *bin;
	size_t           bin_count;
	test_section_t   test;
} manifest_t;

manifest_t *manifest_parse(sds path);
void        manifest_free(manifest_t *m);
i64         manifest_write(sds path, manifest_t *m);

/**
 * Extract package name and version constraint from a dependency entry.
 *
 * @param entry Dependency string like "toml = \"1.0.0\"" or "sds = \"*\""
 * @param name_out Output pointer for package name (must be freed by caller)
 * @param version_out Output pointer for version constraint (must be freed by caller)
 */
void manifest_extract_dep_info(const char *entry, sds *name_out, sds *version_out);

#endif
