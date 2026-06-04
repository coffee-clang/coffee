#include "../build.h"

#include "../coffee.h"
#include "../coffee_features.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>
#include <unistd.h>

int64_t handle_build(options *opts)
{
	char *manifest_path = nullptr;
	if (opts->manifest_path) {
		manifest_path = sdsnew(opts->manifest_path);
	} else {
		manifest_path = project_find_manifest(nullptr);
	}

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml in current directory\n");
		return 1;
	}

	/* Extract project directory from manifest path */
	char  *dir_end = strrchr(manifest_path, '/');
	size_t dir_len;
	sds    makefile_path;
	if (dir_end) {
		dir_len       = (size_t)(dir_end - manifest_path) + 1;
		makefile_path = sdscatprintf(sdsempty(), "%.*sMakefile", (i64)dir_len, manifest_path);
	} else {
		makefile_path = sdsnew("Makefile");
	}

	/* Check for Makefile */
	if (access(makefile_path, F_OK) == 0) {
		/* Build with make */
		sds cmd         = sdsempty();
		sds project_dir = nullptr;
		if (dir_end) {
			project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
		} else {
			project_dir = sdsnew(".");
		}

		cmd = sdscatprintf(cmd, "make -C '%s'", project_dir);
		sdsfree(project_dir);

		if ((i64)opts->release) {
			cmd = sdscatprintf(cmd, " RELEASE=1");
		}
		if ((i64)opts->debug) {
			cmd = sdscatprintf(cmd, " DEBUG=1");
		}
		if (opts->jobs > 0) {
			cmd = sdscatprintf(cmd, " -j%lld", (long long)opts->jobs);
		}

		/* Pass feature flags if specified */
		manifest_t *manifest = manifest_parse(manifest_path);
		if (manifest) {
			char **features       = nullptr;
			size_t features_count = 0;
			if (opts->features) {
				features_parse_cli(opts->features, &features, &features_count);
			}

			if (features_count > 0 || (i64)opts->all_features) {
				const char **requested = nullptr;
				if (features_count > 0) {
					requested = (const char **)features;
				}
				resolved_features_t *resolved = features_resolve(manifest, requested, features_count,
				                                                 opts->all_features, opts->no_default_features);
				if (resolved) {
					size_t dflags_count = 0;
					sds   *dflags       = features_to_compiler_flags(resolved, manifest->package.name, &dflags_count);
					if (dflags_count > 0) {
						cmd = sdscatprintf(cmd, " CFLAGS_EXTRA='");
						for (size_t i = 0; i < dflags_count; i++) {
							cmd = sdscatprintf(cmd, "%s%s", dflags[i], (i + 1 < dflags_count) ? " " : "");
						}
						cmd = sdscatprintf(cmd, "'");
						for (size_t i = 0; i < dflags_count; i++) {
							sdsfree(dflags[i]);
						}
					}
					free(dflags);
					features_free(resolved);
				}
			}

			for (size_t i = 0; i < features_count; i++) {
				sdsfree(features[i]);
			}
			free(features);
			manifest_free(manifest);
		}

		if (opts->verbose) {
			printf("Running: %s\n", cmd);
		}

		i64 status = system(cmd);
		sdsfree(cmd);
		sdsfree(makefile_path);
		sdsfree(manifest_path);

		if (status == -1) {
			fprintf_safe(stderr, "Error: failed to run make\n");
			return 1;
		}
		i64 ret = WEXITSTATUS(status);
		if (ret == 0) {
			printf("Build successful\n");
		} else {
			fprintf_safe(stderr, "Build failed\n");
		}
		return ret;
	}

	/* Fall back to build_project for projects without Makefile */

	manifest_t *manifest = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (manifest == nullptr) {
		sdsfree(makefile_path);
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	char **features       = nullptr;
	size_t features_count = 0;
	if (opts->features) {
		features_parse_cli(opts->features, &features, &features_count);
	}

	build_opts_t build_opts = {
		.verbose             = opts->verbose,
		.release             = opts->release,
		.debug               = opts->debug,
		.locked              = opts->locked,
		.target              = opts->target,
		.target_dir          = opts->target_dir,
		.jobs                = opts->jobs > 0 ? opts->jobs : 1,
		.features            = features,
		.features_count      = features_count,
		.all_features        = opts->all_features,
		.no_default_features = opts->no_default_features,
	};

	i64 ret = build_project(manifest, &build_opts);

	for (size_t i = 0; i < features_count; i++) {
		sdsfree(features[i]);
	}
	free(features);

	manifest_free(manifest);
	sdsfree(makefile_path);

	if (ret == 0) {
		printf("Build successful\n");
	}

	return ret;
}
