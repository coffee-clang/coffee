#include "../build.h"
#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
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

	char *cc        = getenv("CC") != nullptr ? getenv("CC") : "clang";
	sds   inc_flags = sdsnew("-Ideps -Isrc -Iinclude -I.");

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

	/* Collect source and header files */
	glob_t src_glob;
	bool   have_src      = false;
	bool   src_from_glob = false;
	if (m->package.sources_count > 0) {
		have_src = true;
	} else {
		have_src      = (glob("src/*.c", 0, nullptr, &src_glob) == 0);
		src_from_glob = have_src;
	}

	glob_t hdr_glob;
	bool   have_hdr      = false;
	bool   hdr_from_glob = false;
	if (m->package.headers_count > 0) {
		have_hdr = true;
	} else {
		have_hdr      = (glob("include/**/*.h", 0, nullptr, &hdr_glob) == 0);
		hdr_from_glob = have_hdr;
	}

	/* Build argv: cc -fsyntax-only <flags> <sources> <headers> */
	size_t fl_toks   = count_flag_tokens(inc_flags);
	size_t src_count = 0;
	if (have_src) {
		src_count = m->package.sources_count > 0 ? m->package.sources_count : (size_t)src_glob.gl_pathc;
	}

	size_t hdr_count = 0;
	if (have_hdr) {
		hdr_count = m->package.headers_count > 0 ? m->package.headers_count : (size_t)hdr_glob.gl_pathc;
	}

	size_t argc = 1 + 1 + fl_toks + src_count + hdr_count + 1;
	char **argv = (char **)safe_malloc(sizeof(char *) * argc);
	size_t idx  = 0;

	argv[idx++] = cc;
	argv[idx++] = "-fsyntax-only";

	size_t end_idx;
	sds    flags_copy = split_flags_to_argv(inc_flags, argv, idx, &end_idx);
	idx               = end_idx;

	if (m->package.sources_count > 0) {
		for (size_t i = 0; i < m->package.sources_count; i++) {
			argv[idx++] = m->package.sources[i];
		}
	} else if (have_src) {
		for (size_t i = 0; i < (size_t)src_glob.gl_pathc; i++) {
			argv[idx++] = src_glob.gl_pathv[i];
		}
	}

	if (m->package.headers_count > 0) {
		for (size_t i = 0; i < m->package.headers_count; i++) {
			argv[idx++] = m->package.headers[i];
		}
	} else if (have_hdr) {
		for (size_t i = 0; i < (size_t)hdr_glob.gl_pathc; i++) {
			argv[idx++] = hdr_glob.gl_pathv[i];
		}
	}
	argv[idx] = nullptr;

	if (opts->verbose) {
		printf_safe("Running:");
		for (size_t i = 0; i < idx; i++) {
			printf_safe(" %s", argv[i]);
		}
		printf_safe("\n");
	}

	i64 ret = run_command(argv, 0);

	sdsfree(flags_copy);
	safe_free(argv);
	sdsfree(inc_flags);
	if (src_from_glob) {
		globfree(&src_glob);
	}
	if (hdr_from_glob) {
		globfree(&hdr_glob);
	}
	manifest_free(m);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Check failed. Syntax errors found.\n");
		return 1;
	}

	printf_safe("Check complete. No syntax errors found.\n");
	return 0;
}
