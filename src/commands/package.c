#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

int64_t handle_package(options *opts)
{
	(void)opts;
	char *manifest_path = project_find_manifest(nullptr);

	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse manifest\n");
		return 1;
	}

	printf_safe("Packaging project...\n");

	const char *name    = m->package.name != nullptr ? m->package.name : "project";
	const char *version = m->package.version != nullptr ? m->package.version : "0.1.0";

	char package_dir[] = "target/package";
	mkdir("target", 0755);
	mkdir(package_dir, 0755);

	sds tarball = sdscatprintf(sdsempty(), "%s/%s-%s.tar.gz", package_dir, name, version);

	char *tar_argv[] = { "tar", "-czf", tarball, "Coffee.toml", "src/", "tests/", nullptr };

	i64 ret = run_command(tar_argv, RUN_CMD_QUIET);

	if (ret == 0) {
		printf_safe("Package created: %s\n", tarball);
	} else {
		fprintf_safe(stderr, "Error: Packaging failed.\n");
	}

	sdsfree(tarball);

	manifest_free(m);
	return ret;
}
