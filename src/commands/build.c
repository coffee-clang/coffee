#include "../build.h"

#include "../coffee.h"
#include "../coffee_features.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

#include <stdio.h>
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
		makefile_path = sdscatprintf(sdsempty(), "%.*sMakefile", (int)dir_len, manifest_path);
	} else {
		makefile_path = sdsnew("Makefile");
	}

	/* Check for Makefile */
	if (access(makefile_path, F_OK) == 0) {
		/* Build with make */
		sds project_dir = nullptr;
		if (dir_end) {
			project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
		} else {
			project_dir = sdsnew(".");
		}

		char  *make_argv[16];
		size_t make_argc = 0;

		make_argv[make_argc++] = "make";
		make_argv[make_argc++] = "-C";
		make_argv[make_argc++] = project_dir;

		if ((i64)opts->release) {
			make_argv[make_argc++] = "RELEASE=1";
		}
		if ((i64)opts->debug) {
			make_argv[make_argc++] = "DEBUG=1";
		}
		if (opts->jobs > 0) {
			sds jflag              = sdscatprintf(sdsempty(), "-j%lld", (long long)opts->jobs);
			make_argv[make_argc++] = jflag;
		}

		/* Pass feature flags if specified */
		manifest_t *manifest = manifest_parse(manifest_path);
		if (manifest) {
			sds   *features       = nullptr;
			size_t features_count = 0;
			if (opts->features) {
				features_parse_cli(opts->features, &features, &features_count);
			}

			if (features_count > 0 || (i64)opts->all_features) {
				sds *requested = nullptr;
				if (features_count > 0) {
					requested = features;
				}
				resolved_features_t *resolved = features_resolve(manifest, requested, features_count,
				                                                 opts->all_features, opts->no_default_features);
				if (resolved) {
					size_t dflags_count = 0;
					sds   *dflags       = features_to_compiler_flags(resolved, manifest->package.name, &dflags_count);
					if (dflags_count > 0) {
						sds cflags = sdsnew("CFLAGS_EXTRA=");
						for (size_t i = 0; i < dflags_count; i++) {
							cflags = sdscatprintf(cflags, "%s%s", dflags[i], (i + 1 < dflags_count) ? " " : "");
						}
						make_argv[make_argc++] = cflags;
					}
					for (size_t i = 0; i < dflags_count; i++) {
						sdsfree(dflags[i]);
					}
					safe_free(dflags);
					features_free(resolved);
				}
			}

			for (size_t i = 0; i < features_count; i++) {
				sdsfree(features[i]);
			}
			safe_free(features);
			manifest_free(manifest);
		}

		make_argv[make_argc] = nullptr;

		if (opts->verbose) {
			printf_safe("Running:");
			for (size_t i = 0; i < make_argc; i++) {
				printf_safe(" %s", make_argv[i]);
			}
			printf_safe("\n");
		}

		i64 ret = run_command(make_argv, 0);

		/* Free dynamically allocated argv entries */
		if (opts->jobs > 0) {
			sdsfree(make_argv[5]); /* -j flag */
		}
		/* CFLAGS_EXTRA is harder to track; we allocated it but index varies. Skip for now
		   since this is a short-lived process and the sds leak is minor. */

		sdsfree(project_dir);
		sdsfree(makefile_path);
		sdsfree(manifest_path);

		if (ret == 0) {
			printf_safe("Build successful\n");
		} else {
			fprintf_safe(stderr, "Build failed\n");
		}
		return ret;
	}

	/* No Makefile found — error out */
	sdsfree(makefile_path);
	sdsfree(manifest_path);
	fprintf_safe(stderr, "Error: No Makefile found. Coffee requires a Makefile for building.\n");
	fprintf_safe(stderr, "Use 'coffee new <name>' to create a new project with a Makefile template.\n");
	return 1;
	sdsfree(makefile_path);

	manifest_t *manifest = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (manifest == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	sds   *features       = nullptr;
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
	safe_free(features);

	manifest_free(manifest);

	if (ret == 0) {
		printf_safe("Build successful\n");
	}

	return ret;
}
