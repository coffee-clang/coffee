#include "coffee_features.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool feature_in_set(feature_set_t *fs, const char *name)
{
	for (size_t i = 0; i < fs->count; i++) {
		if (fs->names[i] && strcmp(fs->names[i], name) == 0) {
			return true;
		}
	}
	return false;
}

static void feature_set_add(feature_set_t *fs, const char *name)
{
	if (feature_in_set(fs, name)) {
		return;
	}
	fs->names	     = realloc(fs->names, (fs->count + 1) * sizeof(char *));
	fs->names[fs->count] = strdup(name);
	fs->count++;
}

static feature_set_t *get_or_create_set(resolved_features_t *rf, const char *package)
{
	for (size_t i = 0; i < rf->package_count; i++) {
		if (rf->package_names[i] && strcmp(rf->package_names[i], package) == 0) {
			return &rf->packages[i];
		}
	}
	rf->package_names		      = realloc(rf->package_names, (rf->package_count + 1) * sizeof(char *));
	rf->packages			      = realloc(rf->packages, (rf->package_count + 1) * sizeof(feature_set_t));
	rf->package_names[rf->package_count]  = strdup(package);
	rf->packages[rf->package_count].names = NULL;
	rf->packages[rf->package_count].count = 0;
	rf->package_count++;
	return &rf->packages[rf->package_count - 1];
}

resolved_features_t *features_resolve(manifest_t *root, const char **requested, size_t requested_count,
				      bool all_features, bool no_default_features)
{
	if (!root) {
		return NULL;
	}

	resolved_features_t *rf = calloc(1, sizeof(resolved_features_t));
	if (!rf) {
		return NULL;
	}

	feature_set_t *root_set = get_or_create_set(rf, root->package.name ? root->package.name : "root");

	if (all_features) {
		for (size_t i = 0; i < root->features_count; i++) {
			if (root->features[i].name) {
				feature_set_add(root_set, root->features[i].name);
			}
		}
	} else {
		for (size_t i = 0; i < requested_count; i++) {
			if (requested[i]) {
				feature_set_add(root_set, requested[i]);
			}
		}

		if (!no_default_features) {
			for (size_t i = 0; i < root->features_count; i++) {
				if (root->features[i].name && strcmp(root->features[i].name, "default") == 0) {
					for (size_t j = 0; j < root->features[i].deps_count; j++) {
						if (root->features[i].deps[j]) {
							feature_set_add(root_set, root->features[i].deps[j]);
						}
					}
					break;
				}
			}
		}
	}

	for (size_t i = 0; i < root->features_count; i++) {
		if (!root->features[i].name || !feature_in_set(root_set, root->features[i].name)) {
			continue;
		}
		for (size_t j = 0; j < root->features[i].deps_count; j++) {
			char *dep = root->features[i].deps[j];
			if (!dep) {
				continue;
			}
			char *slash = strchr(dep, '/');
			if (slash) {
				size_t	       pkg_len	 = (size_t)(slash - dep);
				char	      *pkg_name	 = strndup(dep, pkg_len);
				char	      *feat_name = slash + 1;
				feature_set_t *pkg_set	 = get_or_create_set(rf, pkg_name);
				feature_set_add(pkg_set, feat_name);
				free(pkg_name);
			}
		}
	}

	return rf;
}

void features_free(resolved_features_t *rf)
{
	if (!rf) {
		return;
	}
	for (size_t i = 0; i < rf->package_count; i++) {
		free(rf->package_names[i]);
		for (size_t j = 0; j < rf->packages[i].count; j++) {
			free(rf->packages[i].names[j]);
		}
		free(rf->packages[i].names);
	}
	free(rf->package_names);
	free(rf->packages);
	free(rf);
}

bool features_is_enabled(resolved_features_t *rf, const char *package, const char *feature)
{
	if (!rf || !package || !feature) {
		return false;
	}
	for (size_t i = 0; i < rf->package_count; i++) {
		if (rf->package_names[i] && strcmp(rf->package_names[i], package) == 0) {
			return feature_in_set(&rf->packages[i], feature);
		}
	}
	return false;
}

char **features_to_compiler_flags(resolved_features_t *rf, const char *package, size_t *out_count)
{
	if (!rf || !package || !out_count) {
		return NULL;
	}

	*out_count	   = 0;
	feature_set_t *set = NULL;

	for (size_t i = 0; i < rf->package_count; i++) {
		if (rf->package_names[i] && strcmp(rf->package_names[i], package) == 0) {
			set = &rf->packages[i];
			break;
		}
	}

	if (!set || set->count == 0) {
		return NULL;
	}

	char **flags = calloc(set->count, sizeof(char *));
	if (!flags) {
		return NULL;
	}

	size_t idx = 0;
	for (size_t i = 0; i < set->count; i++) {
		if (!set->names[i] || strcmp(set->names[i], "default") == 0) {
			continue;
		}
		size_t flag_len = strlen(set->names[i]) + 12;
		flags[idx]	= malloc(flag_len);
		snprintf(flags[idx], flag_len, "-DFEATURE_");
		size_t prefix_len = strlen(flags[idx]);
		for (size_t j = 0; set->names[i][j] != '\0'; j++) {
			char c = set->names[i][j];
			if (c == '-') {
				flags[idx][prefix_len + j] = '_';
			} else if (c >= 'a' && c <= 'z') {
				flags[idx][prefix_len + j] = (char)(c - 32);
			} else {
				flags[idx][prefix_len + j] = c;
			}
		}
		flags[idx][flag_len - 1] = '\0';
		idx++;
	}

	*out_count = idx;
	return flags;
}

void features_parse_cli(const char *cli_string, char ***out_features, size_t *out_count)
{
	if (!cli_string || !out_features || !out_count) {
		*out_features = NULL;
		*out_count    = 0;
		return;
	}

	size_t count = 1;
	for (size_t i = 0; cli_string[i] != '\0'; i++) {
		if (cli_string[i] == ',') {
			count++;
		}
	}

	char **features = calloc(count, sizeof(char *));
	if (!features) {
		*out_features = NULL;
		*out_count    = 0;
		return;
	}

	size_t	    idx = 0;
	const char *p	= cli_string;
	const char *end = p;

	while (*p) {
		end = p;
		while (*end && *end != ',') {
			end++;
		}
		size_t len    = (size_t)(end - p);
		features[idx] = malloc(len + 1);
		memcpy(features[idx], p, len);
		features[idx][len] = '\0';
		idx++;
		p = *end ? end + 1 : end;
	}

	*out_features = features;
	*out_count    = idx;
}
