#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <unistd.h>

static i64 validate_manifest(manifest_t *m)
{
	i64 errors = 0;

	if (m->package.name == nullptr) {
		fprintf_safe(stderr, "Error: [package] name is required\n");
		errors++;
	}
	if (m->package.version == nullptr) {
		fprintf_safe(stderr, "Error: [package] version is required\n");
		errors++;
	}
	if (m->package.edition == nullptr) {
		fprintf_safe(stderr, "Warning: [package] edition not set, defaulting to c23\n");
	}
	if (m->package.description == nullptr) {
		fprintf_safe(stderr, "Warning: [package] description not set\n");
	}
	if (m->package.license == nullptr) {
		fprintf_safe(stderr, "Warning: [package] license not set\n");
	}

	return errors;
}

int64_t handle_check(options *opts)
{
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	printf_safe("Validating Coffee.toml...\n");
	if (validate_manifest(m) > 0) {
		manifest_free(m);
		return 1;
	}
	printf_safe("Manifest is valid.\n");

	printf_safe("Checking source code for syntax errors...\n");

	const char *cc = getenv("CC") != nullptr ? getenv("CC") : "clang";

	/* Build include flags */
	sds inc_flags = sdsnew("-Ideps -Isrc -Iinclude -I.");

	/* Add dependency include directories */
	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		sds   name;
		char *eq = strchr(m->package.dependencies[i], '=');
		if (eq) {
			size_t len = (size_t)(eq - m->package.dependencies[i]);
			while (len > 0 && m->package.dependencies[i][len - 1] == ' ') {
				len--;
			}
			name = sdsnewlen(m->package.dependencies[i], len);
		} else {
			name = sdsnew(m->package.dependencies[i]);
		}

		sds dep_dir = sdscatprintf(sdsempty(), "deps/%s", name);
		if (access(dep_dir, F_OK) == 0) {
			sds inc = sdscatprintf(sdsempty(), "%s/include", dep_dir);
			if (access(inc, F_OK) == 0) {
				inc_flags = sdscatprintf(inc_flags, " -I%s", inc);
			}
			inc_flags = sdscatprintf(inc_flags, " -I%s", dep_dir);
			sdsfree(inc);
		}
		sdsfree(dep_dir);
		sdsfree(name);
	}

	if (m->package.name) {
		inc_flags = sdscatprintf(inc_flags, " -Iinclude/%s", m->package.name);
	}

	/* Collect all source files */
	sds cmd = sdscatprintf(sdsempty(), "%s -fsyntax-only %s", cc, inc_flags);
	sdsfree(inc_flags);

	/* Add sources from manifest */
	if (m->package.sources_count > 0) {
		for (size_t i = 0; i < m->package.sources_count; i++) {
			cmd = sdscatprintf(cmd, " %s", m->package.sources[i]);
		}
	} else {
		/* Fall back to all .c files in src/ */
		cmd = sdscatprintf(cmd, " src/*.c");
	}

	if (m->package.headers_count > 0) {
		for (size_t i = 0; i < m->package.headers_count; i++) {
			cmd = sdscatprintf(cmd, " %s", m->package.headers[i]);
		}
	} else {
		glob_t globbuf;
		if (glob("include/**/*.h", 0, nullptr, &globbuf) == 0) {
			for (size_t i = 0; i < globbuf.gl_pathc; i++) {
				cmd = sdscatprintf(cmd, " %s", globbuf.gl_pathv[i]);
			}
			globfree(&globbuf);
		}
	}

	cmd = sdscatprintf(cmd, " 2>&1");

	if (opts->verbose) {
		printf_safe("Running: %s\n", cmd);
	}

	i64 ret = system(cmd);
	sdsfree(cmd);

	manifest_free(m);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Check failed. Syntax errors found.\n");
		return 1;
	}

	printf_safe("Check complete. No syntax errors found.\n");
	return 0;
}
