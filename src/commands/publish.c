#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>

int64_t handle_publish(options *opts)
{
	(void)opts;

	char *manifest_path = project_find_manifest(NULL);
	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}
	free(manifest_path);

	fprintf(stderr, "Error: 'publish' is not yet supported. Registry write API does not exist.\n");
	return 1;
}
