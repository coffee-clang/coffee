#include "coffee_features.h"

#include "build.h"
#include "safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sds/sds.h>

static bool feature_in_set(feature_set_t *fs, const char *name)
{
	for (size_t i = 0; i < fs->count; i++) {
		if (fs->names[i] != nullptr && strcmp(fs->names[i], name) == 0) {
			return true;
		}
	}
	return false;
}

static bool feature_set_add(feature_set_t *fs, const char *name)
{
	if (feature_in_set(fs, name)) {
		return true;
	}
	fs->names            = safe_realloc(fs->names, (fs->count + 1) * sizeof(sds));
	fs->names[fs->count] = sdsnew(name);
	fs->count++;
	return true;
}

static feature_set_t *get_or_create_set(resolved_features_t *rf, const char *package)
{
	for (size_t i = 0; i < rf->package_count; i++) {
		if (rf->package_names[i] != nullptr && strcmp(rf->package_names[i], package) == 0) {
			return &rf->packages[i];
		}
	}
	rf->package_names = safe_realloc(rf->package_names, (rf->package_count + 1) * sizeof(sds));

	rf->packages = safe_realloc(rf->packages, (rf->package_count + 1) * sizeof(feature_set_t));

	rf->package_names[rf->package_count]  = sdsnew(package);
	rf->packages[rf->package_count].names = nullptr;
	rf->packages[rf->package_count].count = 0;
	rf->package_count++;
	return &rf->packages[rf->package_count - 1];
}

static void resolve_cross_package_refs(manifest_t *manifest, resolved_features_t *rf)
{
	sds pkg = manifest->package.name;
	if (pkg == nullptr) {
		return;
	}
	feature_set_t *set = nullptr;
	for (size_t k = 0; k < rf->package_count; k++) {
		if (rf->package_names[k] != nullptr && strcmp(rf->package_names[k], pkg) == 0) {
			set = &rf->packages[k];
			break;
		}
	}
	if (set == nullptr) {
		return;
	}
	for (size_t i = 0; i < manifest->features_count; i++) {
		if (manifest->features[i].name == nullptr || !feature_in_set(set, manifest->features[i].name)) {
			continue;
		}
		for (size_t j = 0; j < manifest->features[i].deps_count; j++) {
			sds dep = manifest->features[i].deps[j];
			if (dep == nullptr) {
				continue;
			}
			char *slash = strchr(dep, '/');
			if (slash) {
				size_t         pkg_len   = (size_t)(slash - dep);
				sds            pkg_name  = sdsnewlen(dep, pkg_len);
				char          *feat_name = slash + 1;
				feature_set_t *pkg_set   = get_or_create_set(rf, pkg_name);
				feature_set_add(pkg_set, feat_name);

				sdsfree(pkg_name);
			}
		}
	}
}

resolved_features_t *features_resolve(manifest_t *root, sds *requested, size_t requested_count, bool all_features,
                                      bool no_default_features)
{
	if (root == nullptr) {
		return nullptr;
	}

	resolved_features_t *rf = safe_calloc(1, sizeof(resolved_features_t));

	feature_set_t *root_set = get_or_create_set(rf, root->package.name != nullptr ? root->package.name : "root");

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

	/* Single-level cross-package refs from root */
	resolve_cross_package_refs(root, rf);

	/* Transitive resolution: iterate discovered foreign packages until stable */
	bool changed  = true;
	i64  max_iter = 100; /* safety limit */
	while (changed && max_iter-- > 0) {
		changed = false;
		for (size_t pi = 0; pi < rf->package_count; pi++) {
			const char *pkg_name = rf->package_names[pi];
			if (pkg_name == nullptr) {
				continue;
			}
			/* Skip root — already fully resolved */
			if (root->package.name != nullptr && strcmp(pkg_name, root->package.name) == 0) {
				continue;
			}
			/* Find the dep's directory on disk */
			sds dep_dir = dep_resolve_dir(pkg_name);
			if (dep_dir == nullptr) {
				continue;
			}
			/* Load its manifest (Coffee.toml or library.toml) */
			sds         manifest_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", dep_dir);
			manifest_t *dep_manifest  = manifest_parse(manifest_path);
			if (dep_manifest == nullptr) {
				sdsfree(manifest_path);
				manifest_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
				dep_manifest  = manifest_parse(manifest_path);
			}
			sdsfree(manifest_path);
			sdsfree(dep_dir);

			if (dep_manifest == nullptr) {
				continue;
			}

			/* Set the dep's package name in rf if not already set */
			if (dep_manifest->package.name != nullptr) {
				bool found = false;
				for (size_t k = 0; k < rf->package_count; k++) {
					if (rf->package_names[k] != nullptr &&
					    strcmp(rf->package_names[k], dep_manifest->package.name) == 0) {
						found = true;
						break;
					}
				}
				if (!found) {
					/* Add the dep's canonical name to rf */
					feature_set_t *new_set = get_or_create_set(rf, dep_manifest->package.name);
					(void)new_set;
					changed = true;
				}
			}

			size_t before = rf->package_count;
			resolve_cross_package_refs(dep_manifest, rf);
			if (rf->package_count != before) {
				changed = true;
			}
			manifest_free(dep_manifest);
		}
	}

	return rf;
}

void features_free(resolved_features_t *rf)
{
	if (rf == nullptr) {
		return;
	}
	for (size_t i = 0; i < rf->package_count; i++) {
		sdsfree(rf->package_names[i]);
		for (size_t j = 0; j < rf->packages[i].count; j++) {
			sdsfree(rf->packages[i].names[j]);
		}
		safe_free(rf->packages[i].names);
	}
	safe_free(rf->package_names);
	safe_free(rf->packages);
	safe_free(rf);
}

bool features_is_enabled(resolved_features_t *rf, sds package, sds feature)
{
	if (rf == nullptr || package == nullptr || feature == nullptr) {
		return false;
	}
	for (size_t i = 0; i < rf->package_count; i++) {
		if (rf->package_names[i] != nullptr && strcmp(rf->package_names[i], package) == 0) {
			return feature_in_set(&rf->packages[i], feature);
		}
	}
	return false;
}

sds *features_to_compiler_flags(resolved_features_t *rf, sds package, size_t *out_count)
{
	if (rf == nullptr || package == nullptr || out_count == nullptr) {
		return nullptr;
	}

	*out_count         = 0;
	feature_set_t *set = nullptr;

	for (size_t i = 0; i < rf->package_count; i++) {
		if (rf->package_names[i] != nullptr && strcmp(rf->package_names[i], package) == 0) {
			set = &rf->packages[i];
			break;
		}
	}

	if (set == nullptr || set->count == 0) {
		return nullptr;
	}

	sds *flags = safe_calloc(set->count, sizeof(sds));

	size_t idx = 0;
	for (size_t i = 0; i < set->count; i++) {
		if (set->names[i] == nullptr || strcmp(set->names[i], "default") == 0) {
			continue;
		}
		sds flag = sdsnew("-DFEATURE_");
		for (size_t j = 0; set->names[i][j] != '\0'; j++) {
			char c = set->names[i][j];
			if (c == '-') {
				flag = sdscatlen(flag, "_", 1);
			} else if (c >= 'a' && c <= 'z') {
				char upper = (char)(c - 32);
				flag       = sdscatlen(flag, &upper, 1);
			} else {
				flag = sdscatlen(flag, &c, 1);
			}
		}
		flags[idx] = flag;
		idx++;
	}

	*out_count = idx;
	return flags;
}

void features_parse_cli(const char *cli_string, sds **out_features, size_t *out_count)
{
	if (!cli_string || !out_features || out_count == nullptr) {
		*out_features = nullptr;
		*out_count    = 0;
		return;
	}

	size_t count = 1;
	for (size_t i = 0; cli_string[i] != '\0'; i++) {
		if (cli_string[i] == ',') {
			count++;
		}
	}

	sds *features = safe_calloc(count, sizeof(sds));

	size_t      idx = 0;
	const char *p   = cli_string;
	const char *end = p;

	while (*p != '\0') {
		end = p;
		while (*end != '\0' && *end != ',') {
			end++;
		}
		size_t len    = (size_t)(end - p);
		features[idx] = sdsnewlen(p, len);
		idx++;
		p = *end != '\0' ? end + 1 : end;
	}

	*out_features = features;
	*out_count    = idx;
}
