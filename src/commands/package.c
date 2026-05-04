#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

int64_t handle_package(options *)
{
	char *manifest_path = project_find_manifest(NULL);

	if (!manifest_path) {
		fprintf(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (!m) {
		fprintf(stderr, "Error: Could not parse manifest\n");
		return 1;
	}

	printf("Packaging project...\n");

	const char *name	= m->package.name ? m->package.name : "project";
	const char *version = m->package.version ? m->package.version : "0.1.0";

	char package_dir[] = "target/package";
	mkdir("target", 0755);
	mkdir(package_dir, 0755);

	char tarball[4'096];
	snprintf(tarball, sizeof(tarball), "%s/%s-%s.tar.gz", package_dir, name, version);

	char cmd[4'096];
	snprintf(cmd, sizeof(cmd), "tar -czf %s Coffee.toml src/ tests/ 2>/dev/null", tarball);

	int ret = system(cmd);

	if (ret == 0) {
		printf("Package created: %s\n", tarball);
	} else {
		fprintf(stderr, "Error: Packaging failed.\n");
	}

	manifest_free(m);
	return ret;
}
