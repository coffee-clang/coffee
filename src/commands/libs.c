#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>
#include <unistd.h>

static void append_libs_for_pkg(const char *name, sds *buf)
{
	sds dir = pkg_dir(name);
	if (dir == nullptr) {
		return;
	}

	if (access(dir, F_OK) != 0) {
		sdsfree(dir);
		return;
	}

	i64 has_libdir = 0;
	sds libname    = sdsnew(name);

	sds toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dir);

	FILE *fp = fopen(toml_path, "r");
	if (fp) {
		char          errbuf[256];
		toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
		if (conf) {
			/* Read libname override */
			toml_raw_t raw_libname = toml_raw_in(conf, "libname");
			if (raw_libname) {
				char *s;
				if (toml_rtos(raw_libname, &s) == 0 && s) {
					sdsfree(libname);
					libname = sdsnew(s);
					free(s);
				}
			}

			/* Read lib array */
			toml_array_t *lib = toml_array_in(conf, "lib");
			if (lib) {
				i64 n = toml_array_nelem(lib);
				for (i64 i = 0; i < n; i++) {
					toml_raw_t raw = toml_raw_at(lib, i);
					if (raw) {
						char *s;
						if (toml_rtos(raw, &s) == 0 && s) {
							*buf = sdscatprintf(*buf, "-L%s/%s ", dir, s);
							free(s);
							has_libdir = 1;
						}
					}
				}
			}
			toml_free(conf);
		}
		fclose(fp);
	}

	/* Fallback: convention-based library path */
	if (!has_libdir) {
		sds lib_path = sdscatprintf(sdsempty(), "%s/lib", dir);
		if (access(lib_path, F_OK) == 0) {
			*buf = sdscatprintf(*buf, "-L%s ", lib_path);
		}
		sdsfree(lib_path);
	}

	/* Append -l flag */
	*buf = sdscatprintf(*buf, "-l%s ", libname);

	sdsfree(libname);
	sdsfree(toml_path);
	sdsfree(dir);
}

int64_t handle_libs(options *opts)
{
	sds buf = sdsempty();

	if (opts->inputs_num > 1) {
		append_libs_for_pkg(opts->inputs[1], &buf);
	} else {
		char *manifest_path = project_find_manifest(nullptr);
		if (manifest_path == nullptr) {
			sdsfree(buf);
			fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
			return 1;
		}

		manifest_t *m = manifest_parse(manifest_path);
		sdsfree(manifest_path);

		if (m == nullptr) {
			sdsfree(buf);
			fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
			return 1;
		}

		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			sds dep = m->package.dependencies[i];
			if (dep == nullptr) {
				continue;
			}

			sds   name;
			char *eq = strchr(dep, '=');
			if (eq) {
				size_t len = (size_t)(eq - dep);
				while (len > 0 && dep[len - 1] == ' ') {
					len--;
				}
				name = sdsnewlen(dep, len);
			} else {
				name = sdsnew(dep);
			}

			append_libs_for_pkg(name, &buf);
			sdsfree(name);
		}

		manifest_free(m);
	}

	if (sdslen(buf) > 0 && buf[sdslen(buf) - 1] == ' ') {
		buf[sdslen(buf) - 1] = '\0';
		sdsupdatelen(buf);
	}

	printf("%s\n", buf);
	sdsfree(buf);
	return 0;
}
