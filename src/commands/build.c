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
	char *manifest_path = NULL;
	if (opts->manifest_path) {
		manifest_path = strdup(opts->manifest_path);
	} else {
		manifest_path = project_find_manifest(NULL);
	}

	if (!manifest_path) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml in current directory\n");
		return 1;
	}

	/* Extract project directory from manifest path */
	char  *dir_end = strrchr(manifest_path, '/');
	size_t dir_len;
	char   makefile_path[4096];
	if (dir_end) {
		dir_len = (size_t)(dir_end - manifest_path) + 1;
		snprintf_safe(makefile_path, sizeof(makefile_path), "%.*sMakefile", (int)dir_len, manifest_path);
	} else {
		snprintf_safe(makefile_path, sizeof(makefile_path), "Makefile");
	}

	/* Check for Makefile */
	if (access(makefile_path, F_OK) == 0) {
		/* Build with make */
		char  cmd[4096];
		char *project_dir = NULL;
		if (dir_end) {
			project_dir = strndup(manifest_path, (size_t)(dir_end - manifest_path));
		} else {
			project_dir = strdup(".");
		}

		int off = snprintf_safe(cmd, sizeof(cmd), "make -C '%s'", project_dir);
		free(project_dir);

		if (off < 0 || (size_t)off >= sizeof(cmd)) {
			free(manifest_path);
			return 1;
		}

		if ((int)opts->release && (size_t)off < sizeof(cmd)) {
			off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, " RELEASE=1");
		}
		if ((int)opts->debug && (size_t)off < sizeof(cmd)) {
			off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, " DEBUG=1");
		}
		if (opts->jobs > 0 && (size_t)off < sizeof(cmd)) {
			off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, " -j%d", opts->jobs);
		}

		/* Pass feature flags if specified */
		manifest_t *manifest = manifest_parse(manifest_path);
		if (manifest) {
			char **features		  = NULL;
			size_t features_count = 0;
			if (opts->features) {
				features_parse_cli(opts->features, &features, &features_count);
			}

			if (features_count > 0 || (int)opts->all_features) {
				const char **requested = NULL;
				if (features_count > 0) {
					requested = (const char **)features;
				}
				resolved_features_t *resolved = features_resolve(manifest, requested, features_count,
																 opts->all_features, opts->no_default_features);
				if (resolved) {
					size_t dflags_count = 0;
					char **dflags		= features_to_compiler_flags(resolved, manifest->package.name, &dflags_count);
					if (dflags_count > 0) {
						if ((size_t)off < sizeof(cmd)) {
							off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, " CFLAGS_EXTRA=");
							for (size_t i = 0; i < dflags_count && (size_t)off < sizeof(cmd); i++) {
								off += snprintf_safe(cmd + off, sizeof(cmd) - (size_t)off, "%s%s", dflags[i],
													 (i + 1 < dflags_count) ? " " : "");
							}
						}
						for (size_t i = 0; i < dflags_count; i++) {
							free(dflags[i]);
						}
					}
					free(dflags);
					features_free(resolved);
				}
			}

			for (size_t i = 0; i < features_count; i++) {
				free(features[i]);
			}
			free(features);
			manifest_free(manifest);
		}

		if ((size_t)off >= sizeof(cmd)) {
			fprintf_safe(stderr, "Error: build command too long\n");
			free(manifest_path);
			return 1;
		}

		if (opts->verbose) {
			printf("Running: %s\n", cmd);
		}

		int status = system(cmd);

		free(manifest_path);

		if (status == -1) {
			fprintf_safe(stderr, "Error: failed to run make\n");
			return 1;
		}
		int ret = WEXITSTATUS(status);
		if (ret == 0) {
			printf("Build successful\n");
		} else {
			fprintf_safe(stderr, "Build failed\n");
		}
		return ret;
	}

	/* Fall back to build_project for projects without Makefile */

	manifest_t *manifest = manifest_parse(manifest_path);
	free(manifest_path);

	if (!manifest) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	char **features		  = NULL;
	size_t features_count = 0;
	if (opts->features) {
		features_parse_cli(opts->features, &features, &features_count);
	}

	build_opts_t build_opts = {
		.verbose			 = opts->verbose,
		.release			 = opts->release,
		.debug				 = opts->debug,
		.target				 = opts->target,
		.target_dir			 = opts->target_dir,
		.jobs				 = opts->jobs > 0 ? opts->jobs : 1,
		.features			 = features,
		.features_count		 = features_count,
		.all_features		 = opts->all_features,
		.no_default_features = opts->no_default_features,
	};

	int ret = build_project(manifest, &build_opts);

	for (size_t i = 0; i < features_count; i++) {
		free(features[i]);
	}
	free(features);

	manifest_free(manifest);

	if (ret == 0) {
		printf("Build successful\n");
	}

	return ret;
}
