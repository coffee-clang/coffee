#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "../registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

typedef struct {
	i64 major;
	i64 minor;
	i64 patch;
} semver_t;

static bool semver_parse(const char *s, semver_t *v)
{
	if (s == nullptr || *s == '\0') {
		return false;
	}

	v->major = 0;
	v->minor = 0;
	v->patch = 0;

	const char *p = s;
	while (*p >= '0' && *p <= '9') {
		v->major = (v->major * 10) + (*p - '0');
		p++;
	}
	if (*p == '.') {
		p++;
		while (*p >= '0' && *p <= '9') {
			v->minor = (v->minor * 10) + (*p - '0');
			p++;
		}
		if (*p == '.') {
			p++;
			while (*p >= '0' && *p <= '9') {
				v->patch = (v->patch * 10) + (*p - '0');
				p++;
			}
		}
	}

	return true;
}

static i64 semver_cmp(semver_t a, semver_t b)
{
	if (a.major != b.major) {
		return a.major - b.major;
	}
	if (a.minor != b.minor) {
		return a.minor - b.minor;
	}
	return a.patch - b.patch;
}

static bool semver_match(const char *constraint, const char *candidate)
{
	if (constraint == nullptr || strcmp(constraint, "*") == 0) {
		return true;
	}

	semver_t ver;
	if (!semver_parse(candidate, &ver)) {
		return false;
	}

	semver_t    con;
	const char *p = constraint;

	if (*p == '=') {
		if (!semver_parse(p + 1, &con)) {
			return false;
		}
		return semver_cmp(ver, con) == 0;
	}
	if (*p == '^') {
		if (!semver_parse(p + 1, &con)) {
			return false;
		}
		return (bool)(ver.major == con.major && ver.minor >= con.minor);
	}
	if (strncmp(p, ">=", 2) == 0) {
		if (!semver_parse(p + 2, &con)) {
			return false;
		}
		return semver_cmp(ver, con) >= 0;
	}
	if (*p == '>') {
		if (!semver_parse(p + 1, &con)) {
			return false;
		}
		return semver_cmp(ver, con) > 0;
	}
	if (strncmp(p, "<=", 2) == 0) {
		if (!semver_parse(p + 2, &con)) {
			return false;
		}
		return semver_cmp(ver, con) <= 0;
	}
	if (*p == '<') {
		if (!semver_parse(p + 1, &con)) {
			return false;
		}
		return semver_cmp(ver, con) < 0;
	}

	if (!semver_parse(constraint, &con)) {
		return false;
	}
	return semver_cmp(ver, con) == 0;
}

static i64 create_symlink(const char *target, const char *link_path)
{
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		if (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode)) {
			sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", link_path);
			if (system(cmd) != 0) {
				sdsfree(cmd);
				return -1;
			}
			sdsfree(cmd);
		}
	}

	sds   link_copy  = sdsnew(link_path);
	char *last_slash = strrchr(link_copy, '/');
	if (last_slash) {
		*last_slash = '\0';
		sds cmd     = sdscatprintf(sdsempty(), "mkdir -p %s", link_copy);
		sdsfree(link_copy);
		if (system(cmd) != 0) {
			sdsfree(cmd);
			return -1;
		}
		sdsfree(cmd);
	} else {
		sdsfree(link_copy);
	}

	if (symlink(target, link_path) != 0) {
		return -1;
	}

	return 0;
}

int64_t handle_update(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	const char *coffee_home = coffee_home_dir();
	sds         global_deps = sdscatprintf(sdsempty(), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);

	sds project_deps = sdsnew(".coffee/deps");
	mkdir(project_deps, 0755);

	printf("Updating dependencies...\n");

	char *target = nullptr;
	if (opts->inputs_num > 1) {
		target = opts->inputs[1];
	}

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry = m->package.dependencies[i];

		sds name               = nullptr;
		sds version_constraint = nullptr;
		manifest_extract_dep_info(entry, &name, &version_constraint);

		if (name == nullptr) {
			continue;
		}

		if (target != nullptr && strcmp(name, target) != 0) {
			sdsfree(name);
			sdsfree(version_constraint);
			continue;
		}

		printf("  Resolving: %s (%s)\n", name, version_constraint != nullptr ? version_constraint : "*");

		version_list_t *versions = registry_get_versions(name);
		if (versions == nullptr || versions->count == 0) {
			fprintf_safe(stderr, "  Error: Package '%s' not found in registry\n", name);
			sdsfree(name);
			sdsfree(version_constraint);
			continue;
		}

		char *resolved_version = versions->versions[0];

		if (version_constraint != nullptr && strcmp(version_constraint, "*") != 0) {
			if (!semver_match(version_constraint, resolved_version)) {
				fprintf_safe(stderr, "  Warning: No version of '%s' matches constraint '%s' (latest is %s)\n", name,
				             version_constraint, resolved_version);
				sdsfree(name);
				sdsfree(version_constraint);
				registry_free_versions(versions);
				continue;
			}
			printf("  Constraint '%s' matched by version %s\n", version_constraint, resolved_version);
		} else {
			printf("  Selected version: %s\n", resolved_version);
		}

		sds cache_path        = sdscatprintf(sdsempty(), "%s/%s/%s", global_deps, name, resolved_version);
		sds project_link_path = sdscatprintf(sdsempty(), "%s/%s/%s", project_deps, name, resolved_version);

		i64 ret = registry_fetch(name, resolved_version, cache_path);
		if (ret != 0) {
			sdsfree(cache_path);
			sdsfree(project_link_path);
			fprintf_safe(stderr, "  Error: Failed to fetch %s %s\n", name, resolved_version);
			sdsfree(name);
			sdsfree(version_constraint);
			registry_free_versions(versions);
			continue;
		}

		ret = create_symlink(cache_path, project_link_path);
		if (ret != 0) {
			sdsfree(cache_path);
			sdsfree(project_link_path);
			fprintf_safe(stderr, "  Error: Failed to create symlink for %s\n", name);
			sdsfree(name);
			sdsfree(version_constraint);
			registry_free_versions(versions);
			continue;
		}

		printf("  Updated: %s@%s\n", name, resolved_version);

		sdsfree(cache_path);
		sdsfree(project_link_path);
		sdsfree(name);
		sdsfree(version_constraint);
		registry_free_versions(versions);
	}

	sds lockfile_path = sdsnew("Coffee.lock");

	FILE *fp = fopen(lockfile_path, "w");
	if (fp) {
		fprintf_safe(fp, "# This file is automatically generated by coffee.\n");
		fprintf_safe(fp, "# It contains the exact versions of all dependencies.\n");
		fprintf_safe(fp, "version = 1\n");

		fprintf_safe(fp, "\n[package]\n");
		if (m->package.name) {
			fprintf_safe(fp, "name = \"%s\"\n", m->package.name);
		}
		if (m->package.version) {
			fprintf_safe(fp, "version = \"%s\"\n", m->package.version);
		}

		if (m->package.dependencies_count > 0) {
			fprintf_safe(fp, "\n[[dependencies]]\n");
			for (size_t i = 0; i < m->package.dependencies_count; i++) {
				if (m->package.dependencies[i]) {
					fprintf_safe(fp, "%s\n", m->package.dependencies[i]);
				}
			}
		}

		fclose(fp);
		printf("Lockfile updated.\n");
	}

	sdsfree(lockfile_path);
	sdsfree(global_deps);
	sdsfree(project_deps);
	manifest_free(m);
	return 0;
}
