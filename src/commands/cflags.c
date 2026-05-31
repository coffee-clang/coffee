#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>
#include <unistd.h>

static char *pkg_dir(const char *name)
{
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	char *dir = malloc(strlen(home) + strlen("/.coffee/deps/") + strlen(name) + 1);
	if (dir == nullptr) {
		return nullptr;
	}
	snprintf_safe(dir, strlen(home) + strlen("/.coffee/deps/") + strlen(name) + 1, "%s/.coffee/deps/%s", home, name);
	return dir;
}

static void append_cflags_for_pkg(const char *name, char *buf, size_t bufsz, size_t *off)
{
	char *dir = pkg_dir(name);
	if (dir == nullptr) {
		return;
	}

	if (access(dir, F_OK) != 0) {
		free(dir);
		return;
	}

	char toml_path[4096];
	snprintf_safe(toml_path, sizeof(toml_path), "%s/library.toml", dir);

	int   found = 0;
	FILE *fp    = fopen(toml_path, "r");
	if (fp) {
		char          errbuf[256];
		toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
		if (conf) {
			toml_array_t *inc = toml_array_in(conf, "include");
			if (inc) {
				int n = toml_array_nelem(inc);
				for (int i = 0; i < n; i++) {
					toml_raw_t raw = toml_raw_at(inc, i);
					if (raw) {
						char *s;
						if (toml_rtos(raw, &s) == 0 && s) {
							size_t avail = bufsz - *off;
							if (avail > 2) {
								*off += snprintf_safe(buf + *off, avail, "-I%s/%s ", dir, s);
							}
							free(s);
							found = 1;
						}
					}
				}
			}
			toml_free(conf);
		}
		fclose(fp);
	}

	if (!found) {
		char inc_path[4096];
		snprintf_safe(inc_path, sizeof(inc_path), "%s/include", dir);
		if (access(inc_path, F_OK) == 0) {
			size_t avail = bufsz - *off;
			if (avail > 2) {
				*off += snprintf_safe(buf + *off, avail, "-I%s ", inc_path);
			}
		}
	}

	free(dir);
}

int64_t handle_cflags(options *opts)
{
	char   buf[8192];
	size_t off = 0;
	buf[0]     = '\0';

	if (opts->inputs_num > 1) {
		append_cflags_for_pkg(opts->inputs[1], buf, sizeof(buf), &off);
	} else {
		char *manifest_path = project_find_manifest(nullptr);
		if (manifest_path == nullptr) {
			fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
			return 1;
		}

		manifest_t *m = manifest_parse(manifest_path);
		free(manifest_path);

		if (m == nullptr) {
			fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
			return 1;
		}

		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			char *dep = m->package.dependencies[i];
			if (dep == nullptr) {
				continue;
			}

			char  name[256];
			char *eq = strchr(dep, '=');
			if (eq) {
				size_t len = (size_t)(eq - dep);
				while (len > 0 && dep[len - 1] == ' ') {
					len--;
				}
				if (len >= sizeof(name)) {
					len = sizeof(name) - 1;
				}
				memccpy(name, dep, '\0', len);
				name[len] = '\0';
			} else {
				snprintf_safe(name, sizeof(name), "%s", dep);
			}

			append_cflags_for_pkg(name, buf, sizeof(buf), &off);
		}

		manifest_free(m);
	}

	if (off > 0 && buf[off - 1] == ' ') {
		buf[off - 1] = '\0';
		off--;
	}

	printf("%s\n", buf);
	return 0;
}
