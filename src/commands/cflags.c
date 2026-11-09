#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>
#include <unistd.h>

static void append_cflags_for_pkg(const char *name, sds *buf)
{
	sds dir = pkg_dir(name);
	if (dir == nullptr) {
		return;
	}

	if (safe_access(dir, F_OK) != 0) {
		sdsfree(dir);
		return;
	}

	sds flags   = sdsempty();
	sds compile = sdsempty();
	sds link    = sdsempty();
	dep_add_flags(dir, name, &flags, nullptr, nullptr);
	dep_split_flags(flags, false, &compile, &link);
	*buf = flags_append_unquoted(*buf, compile);

	sdsfree(link);
	sdsfree(compile);
	sdsfree(flags);
	sdsfree(dir);
}

int64_t handle_cflags(options *opts)
{
	sds buf = sdsempty();

	if (opts->inputs_num > 1) {
		append_cflags_for_pkg(opts->inputs[1], &buf);
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

			append_cflags_for_pkg(name, &buf);
			sdsfree(name);
		}

		manifest_free(m);
	}

	if (sdslen(buf) > 0 && buf[sdslen(buf) - 1] == ' ') {
		buf[sdslen(buf) - 1] = '\0';
		sdsupdatelen(buf);
	}

	printf_safe("%s\n", buf);
	sdsfree(buf);
	return 0;
}
