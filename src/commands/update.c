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
	int major;
	int minor;
	int patch;
} semver_t;

static bool semver_parse(const char *s, semver_t *v)
{
	if (!s || *s == '\0') {
		return false;
	}

	v->major = 0;
	v->minor = 0;
	v->patch = 0;

	const char *p = s;
	while (*p >= '0' && *p <= '9') {
		v->major = v->major * 10 + (*p - '0');
		p++;
	}
	if (*p == '.') {
		p++;
		while (*p >= '0' && *p <= '9') {
			v->minor = v->minor * 10 + (*p - '0');
			p++;
		}
		if (*p == '.') {
			p++;
			while (*p >= '0' && *p <= '9') {
				v->patch = v->patch * 10 + (*p - '0');
				p++;
			}
		}
	}

	return true;
}

static int semver_cmp(semver_t a, semver_t b)
{
	if (a.major != b.major) {
		return a.major - b.major;
	}
	if (a.minor != b.minor) {
		return a.minor - b.minor;
	}
	return a.patch - b.patch;
}

static bool semver_match(const char *constraint, const char *version)
{
	if (!constraint || strcmp(constraint, "*") == 0) {
		return true;
	}

	semver_t ver;
	if (!semver_parse(version, &ver)) {
		return false;
	}

	semver_t	con;
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
		return ver.major == con.major && ver.minor >= con.minor;
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

static int create_symlink(const char *target, const char *link_path)
{
	struct stat st;
	if (lstat(link_path, &st) == 0) {
		if (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode)) {
			char cmd[8'192];
			snprintf(cmd, sizeof(cmd), "rm -rf %s", link_path);
			if (system(cmd) != 0) {
				return -1;
			}
		}
	}

	char *link_copy	 = strdup(link_path);
	char *last_slash = strrchr(link_copy, '/');
	if (last_slash) {
		*last_slash = '\0';
		char cmd[8'192];
		snprintf(cmd, sizeof(cmd), "mkdir -p %s", link_copy);
		free(link_copy);
		if (system(cmd) != 0) {
			return -1;
		}
	} else {
		free(link_copy);
	}

	if (symlink(target, link_path) != 0) {
		return -1;
	}

	return 0;
}

int64_t handle_update(options *opts)
{
	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (!m) {
		fprintf(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	const char *coffee_home = coffee_home_dir();
	char		global_deps[4'096];
	snprintf(global_deps, sizeof(global_deps), "%s/deps", coffee_home);
	mkdir(global_deps, 0755);

	char project_deps[4'096];
	snprintf(project_deps, sizeof(project_deps), ".coffee/deps");
	mkdir(project_deps, 0755);

	printf("Updating dependencies...\n");

	char *target = NULL;
	if (opts->inputs_num > 1) {
		target = opts->inputs[1];
	}

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		const char *entry = m->package.dependencies[i];

		char *name				 = NULL;
		char *version_constraint = NULL;
		manifest_extract_dep_info(entry, &name, &version_constraint);

		if (!name) {
			continue;
		}

		if (target && strcmp(name, target) != 0) {
			free(name);
			free(version_constraint);
			continue;
		}

		printf("  Resolving: %s (%s)\n", name, version_constraint ? version_constraint : "*");

		version_list_t *versions = registry_get_versions(name);
		if (!versions || versions->count == 0) {
			fprintf(stderr, "  Error: Package '%s' not found in registry\n", name);
			free(name);
			free(version_constraint);
			continue;
		}

		char *resolved_version = versions->versions[0];

		if (version_constraint && strcmp(version_constraint, "*") != 0) {
			if (!semver_match(version_constraint, resolved_version)) {
				fprintf(stderr, "  Warning: No version of '%s' matches constraint '%s' (latest is %s)\n", name,
						version_constraint, resolved_version);
				free(name);
				free(version_constraint);
				registry_free_versions(versions);
				continue;
			}
			printf("  Constraint '%s' matched by version %s\n", version_constraint, resolved_version);
		} else {
			printf("  Selected version: %s\n", resolved_version);
		}

		char cache_path[4'096];
		snprintf(cache_path, sizeof(cache_path), "%s/%s/%s", global_deps, name, resolved_version);

		char project_link_path[4'096];
		snprintf(project_link_path, sizeof(project_link_path), "%s/%s/%s", project_deps, name, resolved_version);

		int ret = registry_fetch(name, resolved_version, cache_path);
		if (ret != 0) {
			fprintf(stderr, "  Error: Failed to fetch %s %s\n", name, resolved_version);
			free(name);
			free(version_constraint);
			registry_free_versions(versions);
			continue;
		}

		ret = create_symlink(cache_path, project_link_path);
		if (ret != 0) {
			fprintf(stderr, "  Error: Failed to create symlink for %s\n", name);
			free(name);
			free(version_constraint);
			registry_free_versions(versions);
			continue;
		}

		printf("  Updated: %s@%s\n", name, resolved_version);

		free(name);
		free(version_constraint);
		registry_free_versions(versions);
	}

	char lockfile_path[4'096];
	snprintf(lockfile_path, sizeof(lockfile_path), "Coffee.lock");

	FILE *fp = fopen(lockfile_path, "w");
	if (fp) {
		fprintf(fp, "# This file is automatically generated by coffee.\n");
		fprintf(fp, "# It contains the exact versions of all dependencies.\n");
		fprintf(fp, "version = 1\n");

		fprintf(fp, "\n[package]\n");
		if (m->package.name) {
			fprintf(fp, "name = \"%s\"\n", m->package.name);
		}
		if (m->package.version) {
			fprintf(fp, "version = \"%s\"\n", m->package.version);
		}

		if (m->package.dependencies_count > 0) {
			fprintf(fp, "\n[[dependencies]]\n");
			for (size_t i = 0; i < m->package.dependencies_count; i++) {
				if (m->package.dependencies[i]) {
					fprintf(fp, "%s\n", m->package.dependencies[i]);
				}
			}
		}

		fclose(fp);
		printf("Lockfile updated.\n");
	}

	manifest_free(m);
	return 0;
}
