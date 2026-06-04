#include "project.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgen.h>
#include <sds/sds.h>
#include <sys/stat.h>
#include <unistd.h>

static i64 file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

sds project_find_manifest(sds start_dir)
{
	sds dir;

	if (start_dir) {
		dir = sdsnew(start_dir);
	} else {
		char *cwd_buf = getcwd(nullptr, 0);
		if (cwd_buf == nullptr) {
			return nullptr;
		}
		dir = sdsnew(cwd_buf);
		free(cwd_buf);
	}

	while (1) {
		sds path = sdscatprintf(sdsempty(), "%s/Coffee.toml", dir);
		if (file_exists(path)) {
			sdsfree(dir);
			return path;
		}
		sdsfree(path);

		sds   parent     = sdsdup(dir);
		char *parent_dir = dirname(parent);
		if (strcmp(parent_dir, dir) == 0) {
			sdsfree(parent);
			sdsfree(dir);
			return nullptr;
		}

		sdsfree(dir);
		dir = sdsnew(parent_dir);
		sdsfree(parent);
	}
}

manifest_t *project_load_manifest(sds path)
{
	return manifest_parse(path);
}

sds project_get_name(manifest_t *m)
{
	if (m == nullptr || m->package.name == nullptr) {
		return nullptr;
	}
	return sdsnew(m->package.name);
}

sds pkg_dir(const char *name)
{
	/* Check local deps/ first */
	sds local = sdscatprintf(sdsempty(), "deps/%s", name);
	if (access(local, F_OK) == 0) {
		return local;
	}
	sdsfree(local);

	/* Check vendor/ next */
	sds vendor_dir = sdscatprintf(sdsempty(), "vendor/%s", name);
	if (access(vendor_dir, F_OK) == 0) {
		return vendor_dir;
	}
	sdsfree(vendor_dir);

	/* Fallback to global cache */
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	return sdscatfmt(sdsnew(home), "/.coffee/deps/%s", name);
}

i64 create_dir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0) {
		return 0;
	}
	return mkdir(path, 0755);
}

i64 create_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");
	if (fp == nullptr) {
		return -1;
	}
	fprintf_safe(fp, "%s", content);
	fclose(fp);
	return 0;
}

sds resolve_dep_version(const char *dep_dir)
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
