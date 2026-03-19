#include "../coffee.h"
#include "../coffee_features.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int64_t handle_bench(options *opts)
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

	const char *cc	       = getenv("CC") ? getenv("CC") : "clang";
	const char *output_dir = opts->target_dir ? opts->target_dir : "target/release";

	char cmd[4'096];
	snprintf(cmd, sizeof(cmd), "mkdir -p %s", output_dir);
	system(cmd);

	if (access("benches", F_OK) != 0) {
		printf("No benchmarks found (no 'benches' directory)\n");
		manifest_free(manifest);
		return 0;
	}

	char dflags[1'024] = "";
	if (opts->features) {
		char **features	      = NULL;
		size_t features_count = 0;
		features_parse_cli(opts->features, &features, &features_count);

		resolved_features_t *resolved = features_resolve(manifest, (const char **)features, features_count,
								 opts->all_features, opts->no_default_features);

		if (resolved) {
			size_t dflags_count = 0;
			char **dflags_arr = features_to_compiler_flags(resolved, manifest->package.name, &dflags_count);
			for (size_t i = 0; i < dflags_count; i++) {
				size_t new_len	  = strlen(dflags) + strlen(dflags_arr[i]) + 2;
				char  *new_dflags = malloc(new_len);
				snprintf(new_dflags, new_len, "%s %s", dflags, dflags_arr[i]);
				memcpy(dflags, new_dflags, new_len > sizeof(dflags) ? sizeof(dflags) : new_len);
				free(new_dflags);
				free(dflags_arr[i]);
			}
			free(dflags_arr);
			features_free(resolved);
		}

		for (size_t i = 0; i < features_count; i++) {
			free(features[i]);
		}
		free(features);
	}

	printf("Running benchmarks...\n");

	snprintf(cmd, sizeof(cmd), "for f in benches/*.c; do "
				   "  echo \"Benchmarking $f\"; "
				   "  %s -O3 %s $f -o %s/$(basename $f .c) -Isrc 2>&1 && "
				   "  %s/$(basename $f .c); "
				   "done",
		 cc, dflags, output_dir, output_dir);

	int ret = system(cmd);

	manifest_free(manifest);
	return ret;
}
