#ifndef MANIFEST_H_
#define MANIFEST_H_

#include "coffee.h"

#include <toml.h>

typedef struct {
	char  *name;
	char  *version;
	char  *edition;
	char  *description;
	char  *license;
	char  *repository;
	char  *authors;
	char **dependencies;
	size_t dependencies_count;
	char **sources;
	size_t sources_count;
	char **headers;
	size_t headers_count;
} package_t;

typedef struct {
	package_t package;
	char	 *name;
	char	 *version;
	char	 *path;
	char	 *git;
	char	 *branch;
	char	 *tag;
	char	 *rev;
	bool	  optional;
} dependency_t;

typedef struct {
	dependency_t *deps;
	size_t		  deps_count;
} dependencies_t;

typedef struct {
	char  *name;
	char **deps;
	size_t deps_count;
} feature_def_t;

typedef struct {
	package_t	   package;
	dependencies_t dependencies;
	feature_def_t *features;
	size_t		   features_count;
} manifest_t;

manifest_t *manifest_parse(const char *path);
void		manifest_free(manifest_t *m);
int			manifest_write(const char *path, manifest_t *m);

/**
 * Extract package name and version constraint from a dependency entry.
 *
 * @param entry Dependency string like "toml = \"1.0.0\"" or "sds = \"*\""
 * @param name_out Output pointer for package name (must be freed by caller)
 * @param version_out Output pointer for version constraint (must be freed by caller)
 */
void manifest_extract_dep_info(const char *entry, char **name_out, char **version_out);

#endif
