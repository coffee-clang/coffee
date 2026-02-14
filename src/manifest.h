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
	size_t	      deps_count;
} dependencies_t;

typedef struct {
	package_t      package;
	dependencies_t dependencies;
} manifest_t;

manifest_t *manifest_parse(const char *path);
void	    manifest_free(manifest_t *m);
int	    manifest_write(const char *path, manifest_t *m);

#endif
