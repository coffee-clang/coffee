#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

int64_t handle_run(options *opts)
{
	char *manifest_path = project_find_manifest(NULL);

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

	build_opts_t build_opts = {
		.verbose    = opts->verbose,
		.release    = false,
		.debug	    = false,
		.target	    = NULL,
		.target_dir = NULL,
		.jobs	    = 1,
	};

	char **args = NULL;
	int    argc = 0;
	if (opts->inputs_num > 1) {
		args = &opts->inputs[1];
		argc = opts->inputs_num - 1;
	}

	int ret = build_run(manifest, &build_opts, args, argc);
	manifest_free(manifest);

	return ret;
}
