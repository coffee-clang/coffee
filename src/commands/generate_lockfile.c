#include "../build.h"
#include "../coffee.h"
#include "../coffee_features.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>
#include <unistd.h>

/*
 * Resolve git commit SHA for a dependency directory.
 * Runs "git rev-parse HEAD" in dep_dir.
 * Returns a new sds with the SHA, or nullptr on error.
 */
static sds resolve_git_commit(const char *dep_dir)
{
	if (dep_dir == nullptr || dep_dir[0] == '\0') {
		return nullptr;
	}
	/* Check if it's a git repo */
	sds git_dir = sdscatprintf(sdsempty(), "%s/.git", dep_dir);
	if (access(git_dir, F_OK) != 0) {
		sdsfree(git_dir);
		return nullptr;
	}
	sdsfree(git_dir);

	/* Run git rev-parse HEAD */
	sds   cmd  = sdscatprintf(sdsempty(), "cd '%s' && git rev-parse HEAD 2>/dev/null", dep_dir);
	FILE *pipe = popen(cmd, "r");
	sdsfree(cmd);
	if (pipe == nullptr) {
		return nullptr;
	}
	char buf[128] = { 0 };
	if (fgets(buf, sizeof(buf), pipe) == nullptr) {
		pclose(pipe);
		return nullptr;
	}
	pclose(pipe);
	/* Strip trailing newline */
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
	}
	if (buf[0] == '\0') {
		return nullptr;
	}
	return sdsnew(buf);
}

/* Find a dependency_t by name in the manifest's structured deps */
static dependency_t *find_dep_by_name(manifest_t *m, const char *name)
{
	if (m == nullptr || name == nullptr) {
		return nullptr;
	}
	for (size_t i = 0; i < m->dependencies.deps_count; i++) {
		if (m->dependencies.deps[i].name != nullptr && strcmp(m->dependencies.deps[i].name, name) == 0) {
			return &m->dependencies.deps[i];
		}
	}
	return nullptr;
}

int64_t handle_generate_lockfile(options *opts)
{
	(void)opts;
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *manifest = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (manifest == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	printf("Generating lockfile: Coffee.lock\n");

	lockfile_t lf;
	memset(&lf, 0, sizeof(lf));
	lf.version = 1;

	if (manifest->package.name) {
		lf.package_name = sdsnew(manifest->package.name);
	}
	if (manifest->package.version) {
		lf.package_version = sdsnew(manifest->package.version);
	}

	/* Resolve each dependency */
	lf.deps_count = manifest->package.dependencies_count;
	if (lf.deps_count > 0) {
		lf.deps = calloc(lf.deps_count, sizeof(lockfile_dep_t));
		if (lf.deps == nullptr) {
			manifest_free(manifest);
			return 1;
		}
		for (size_t i = 0; i < lf.deps_count; i++) {
			sds dep_name = nullptr;
			sds dep_vers = nullptr;
			manifest_extract_dep_info(manifest->package.dependencies[i], &dep_name, &dep_vers);

			if (dep_name == nullptr) {
				continue;
			}

			lf.deps[i].name = sdsnew(dep_name);

			/* Check if this is a git dependency — store the git URL in path for reference */
			dependency_t *structured = find_dep_by_name(manifest, dep_name);
			bool          is_git_dep = (structured != nullptr && structured->git != nullptr) != 0;

			/* Resolve the dep directory */
			sds dep_dir = dep_resolve_dir(dep_name);
			if (dep_dir != nullptr) {
				lf.deps[i].path    = sdsnew(dep_dir);
				lf.deps[i].version = resolve_dep_version(dep_dir);

				/* For git deps, record the pinned commit SHA */
				if (is_git_dep) {
					sds commit = resolve_git_commit(dep_dir);
					if (commit != nullptr) {
						lf.deps[i].commit = commit;
					}
				}

				sdsfree(dep_dir);
			} else {
				/* Dep not found locally; record the manifest version as-is */
				lf.deps[i].path    = sdsnew("");
				lf.deps[i].version = dep_vers != nullptr ? sdsnew(dep_vers) : sdsnew("*");
			}

			sdsfree(dep_name);
			sdsfree(dep_vers);
		}
	}

	i64 ret = lockfile_write("Coffee.lock", &lf);

	/* Clean up lockfile_t contents (no lockfile_free since it's stack-allocated) */
	sdsfree(lf.package_name);
	sdsfree(lf.package_version);
	for (size_t i = 0; i < lf.deps_count; i++) {
		sdsfree(lf.deps[i].name);
		sdsfree(lf.deps[i].version);
		sdsfree(lf.deps[i].path);
		sdsfree(lf.deps[i].commit);
	}
	free(lf.deps);

	manifest_free(manifest);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Could not write Coffee.lock\n");
		return 1;
	}

	printf("Lockfile generated successfully.\n");
	return 0;
}
