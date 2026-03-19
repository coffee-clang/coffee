#ifndef FEATURES_H_
#define FEATURES_H_

#include "manifest.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	char **names;
	size_t count;
} feature_set_t;

typedef struct {
	feature_set_t *packages;
	char	     **package_names;
	size_t	       package_count;
} resolved_features_t;

resolved_features_t *features_resolve(manifest_t *root, const char **requested, size_t requested_count,
				      bool all_features, bool no_default_features);

void features_free(resolved_features_t *rf);

bool features_is_enabled(resolved_features_t *rf, const char *package, const char *feature);

char **features_to_compiler_flags(resolved_features_t *rf, const char *package, size_t *out_count);

void features_parse_cli(const char *cli_string, char ***out_features, size_t *out_count);

#endif
