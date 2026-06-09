#include "../build.h"
#include "../coffee.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>

/*
 * Read [dependencies] from a library.toml file.
 * Returns a TOML table of dependencies, or nullptr.
 */
static toml_table_t *read_dep_table(const char *dep_dir)
{
	sds   toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
	FILE *fp        = fopen(toml_path, "r");
	sdsfree(toml_path);
	if (fp == nullptr) {
		return nullptr;
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		return nullptr;
	}

	toml_table_t *deps = toml_table_in(conf, "dependencies");
	if (deps == nullptr) {
		toml_free(conf);
		return nullptr;
	}

	/* We need to keep conf alive while deps is in use, but we can't return both.
	 * Instead, we return conf and let caller free it. deps is a borrowed reference. */
	/* Actually toml_table_in returns a borrowed reference — the caller must free conf.
	 * Let's just read all dep names out now. */
	return conf;
}

static void print_tree(const char *dep_dir, const char *dep_name, const char *version, const char *prefix, bool is_last,
                       i64 depth, i64 max_depth)
{
	if (depth > max_depth) {
		return;
	}

	const char *connector = (int)is_last ? "└── " : "├── ";
	printf_safe("%s%s%s", prefix, connector, dep_name != nullptr ? dep_name : "?");

	if (version != nullptr && strcmp(version, "*") != 0) {
		printf_safe(" v%s", version);
	}
	printf_safe("\n");

	if (depth >= max_depth || dep_dir == nullptr || dep_dir[0] == '\0') {
		return;
	}

	/* Read library.toml from the dep directory for transitive deps */
	toml_table_t *conf = read_dep_table(dep_dir);
	if (conf == nullptr) {
		return;
	}

	toml_table_t *deps = toml_table_in(conf, "dependencies");
	if (deps == nullptr) {
		toml_free(conf);
		return;
	}

	/* Count entries */
	size_t dep_count = 0;
	for (i64 i = 0;; i++) {
		const char *key = toml_key_in(deps, i);
		if (key == nullptr) {
			break;
		}
		dep_count++;
	}

	sds child_prefix = sdscatfmt(sdsnew(prefix), "%s", (int)is_last ? "    " : "│   ");

	size_t idx = 0;
	for (i64 i = 0; idx < dep_count; i++) {
		const char *key = toml_key_in(deps, i);
		if (key == nullptr) {
			break;
		}
		idx++;

		/* Resolve transitive dep by name */
		sds child_dir = dep_resolve_dir(key);
		sds child_ver = nullptr;
		if (child_dir != nullptr) {
			/* Read version from library.toml */
			toml_datum_t ver = toml_string_in(deps, key);
			if (ver.ok) {
				child_ver = sdsnew(ver.u.s);
				free(ver.u.s);
			}
		}

		print_tree(child_dir, key, child_ver, child_prefix, (bool)(idx == dep_count), depth + 1, max_depth);

		sdsfree(child_dir);
		sdsfree(child_ver);
	}

	sdsfree(child_prefix);
	toml_free(conf);
}

int64_t handle_tree(options *opts)
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

	const char *name    = m->package.name != nullptr ? m->package.name : "project";
	const char *version = m->package.version != nullptr ? m->package.version : "0.1.0";

	printf_safe("%s v%s\n", name, version);

	/* Try to use lockfile first for resolved paths */
	lockfile_t *lf = lockfile_parse("Coffee.lock");

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		sds dep_name    = nullptr;
		sds dep_version = nullptr;
		manifest_extract_dep_info(m->package.dependencies[i], &dep_name, &dep_version);

		if (dep_name == nullptr) {
			continue;
		}

		/* Get path from lockfile, or resolve */
		sds dep_path = nullptr;
		if (lf != nullptr) {
			lockfile_dep_t *locked = lockfile_find_dep(lf, dep_name);
			if (locked != nullptr && locked->path != nullptr && locked->path[0] != '\0') {
				dep_path = sdsnew(locked->path);
			}
		}
		if (dep_path == nullptr) {
			dep_path = dep_resolve_dir(dep_name);
		}

		bool is_last = (i == m->package.dependencies_count - 1);
		print_tree(dep_path, dep_name, dep_version, "", is_last, 0, 3);

		sdsfree(dep_path);
		sdsfree(dep_name);
		sdsfree(dep_version);
	}

	lockfile_free(lf);
	manifest_free(m);
	return 0;
}
