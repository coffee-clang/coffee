#include "../build.h"

#include "../coffee.h"
#include "../coffee_features.h"
#include "../manifest.h"
#include "../project.h"

int64_t handle_build(options *opts)
{
	char *manifest_path = NULL;
	if (opts->manifest_path) {
		manifest_path = strdup(opts->manifest_path);
	} else {
		manifest_path = project_find_manifest(NULL);
	}

	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml in current directory\n");
		return 1;
	}

	manifest_t *manifest = manifest_parse(manifest_path);
	free(manifest_path);

	if (!manifest) {
		fprintf(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	char **features	      = NULL;
	size_t features_count = 0;
	if (opts->features) {
		features_parse_cli(opts->features, &features, &features_count);
	}

	build_opts_t build_opts = {
		.verbose	     = opts->verbose,
		.release	     = opts->release,
		.debug		     = opts->debug,
		.target		     = opts->target,
		.target_dir	     = opts->target_dir,
		.jobs		     = opts->jobs > 0 ? opts->jobs : 1,
		.features	     = features,
		.features_count	     = features_count,
		.all_features	     = opts->all_features,
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
