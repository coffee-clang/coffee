#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>

static int validate_manifest(manifest_t *m)
{
	int errors = 0;

	if (!m->package.name) {
		fprintf_safe(stderr, "Error: [package] name is required\n");
		errors++;
	}
	if (!m->package.version) {
		fprintf_safe(stderr, "Error: [package] version is required\n");
		errors++;
	}
	if (!m->package.edition) {
		fprintf_safe(stderr, "Warning: [package] edition not set, defaulting to c23\n");
	}
	if (!m->package.description) {
		fprintf_safe(stderr, "Warning: [package] description not set\n");
	}
	if (!m->package.license) {
		fprintf_safe(stderr, "Warning: [package] license not set\n");
	}

	return errors;
}

int64_t handle_check(options *opts)
{
	char *manifest_path = project_find_manifest(NULL);
	if (!manifest_path) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	free(manifest_path);

	if (!m) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	printf("Validating Coffee.toml...\n");
	if (validate_manifest(m) > 0) {
		manifest_free(m);
		return 1;
	}
	printf("Manifest is valid.\n");

	printf("Checking source code for syntax errors...\n");

	const char *cc = getenv("CC") != nullptr ? getenv("CC") : "clang";

	/* Build include flags */
	char   inc_flags[4096] = "-Ideps -Isrc -Iinclude -I.";
	size_t off			   = strlen(inc_flags);

	if (m->package.name) {
		off += snprintf(inc_flags + off, sizeof(inc_flags) - off, " -Iinclude/%s", m->package.name);
	}

	/* Collect all source files */
	char cmd[8192];
	off = snprintf(cmd, sizeof(cmd), "%s -fsyntax-only %s", cc, inc_flags);

	/* Add sources from manifest */
	if (m->package.sources_count > 0) {
		for (size_t i = 0; i < m->package.sources_count && off < sizeof(cmd); i++) {
			off += snprintf(cmd + off, sizeof(cmd) - off, " %s", m->package.sources[i]);
		}
	} else {
		/* Fall back to all .c files in src/ */
		off += snprintf(cmd + off, sizeof(cmd) - off, " src/*.c");
	}

	if (m->package.headers_count > 0) {
		for (size_t i = 0; i < m->package.headers_count && off < sizeof(cmd); i++) {
			off += snprintf(cmd + off, sizeof(cmd) - off, " %s", m->package.headers[i]);
		}
	} else {
		glob_t globbuf;
		if (glob("include/**/*.h", 0, NULL, &globbuf) == 0) {
			for (size_t i = 0; i < globbuf.gl_pathc && off < sizeof(cmd); i++) {
				off += snprintf(cmd + off, sizeof(cmd) - off, " %s", globbuf.gl_pathv[i]);
			}
			globfree(&globbuf);
		}
	}

	off += snprintf(cmd + off, sizeof(cmd) - off, " 2>&1");

	if (opts->verbose) {
		printf("Running: %s\n", cmd);
	}

	int ret = system(cmd);

	manifest_free(m);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Check failed. Syntax errors found.\n");
		return 1;
	}

	printf("Check complete. No syntax errors found.\n");
	return 0;
}
