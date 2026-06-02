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
 * Read version from a dep's library.toml, or return "*".
 */
static sds resolve_dep_version(const char *dep_dir)
{
	sds   toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
	FILE *fp        = fopen(toml_path, "r");
	if (fp == nullptr) {
		sdsfree(toml_path);
		return sdsnew("*");
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	sds version = nullptr;
	if (conf) {
		toml_datum_t ver = toml_string_in(conf, "version");
		if (ver.ok) {
			version = sdsnew(ver.u.s);
			free(ver.u.s);
		}
		toml_free(conf);
	}

	sdsfree(toml_path);

	if (version == nullptr) {
		version = sdsnew("*");
	}
	return version;
}

int64_t handle_generate_lockfile(options *opts)
{
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

			/* Resolve the dep directory */
			sds dep_dir = dep_resolve_dir(dep_name);
			if (dep_dir != nullptr) {
				lf.deps[i].path    = sdsnew(dep_dir);
				lf.deps[i].version = resolve_dep_version(dep_dir);
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
