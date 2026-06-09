#include "../build.h"
#include "../coffee.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int64_t handle_test(options *opts)
{
	/* Find and parse manifest */
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: No Coffee.toml found\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		sdsfree(manifest_path);
		return 1;
	}

	char *dir_end = strrchr(manifest_path, '/');
	sds   project_dir;
	if (dir_end) {
		project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = sdsnew(".");
	}
	sdsfree(manifest_path);

	/* Determine test sources: from [test] or default glob */
	sds   *test_srcs  = nullptr;
	size_t test_count = 0;
	bool   own_srcs   = false;

	if (m->test.sources_count > 0) {
		test_srcs  = m->test.sources;
		test_count = m->test.sources_count;
	} else {
		glob_t g;
		sds    pattern = sdscatprintf(sdsempty(), "%s/tests/*.c", project_dir);
		if (glob(pattern, 0, nullptr, &g) == 0 && g.gl_pathc > 0) {
			test_count = g.gl_pathc;
			test_srcs  = calloc(test_count, sizeof(sds));
			if (test_srcs) {
				own_srcs = true;
				for (size_t i = 0; i < test_count; i++) {
					test_srcs[i] = sdsnew(g.gl_pathv[i]);
				}
			}
		}
		globfree(&g);
		sdsfree(pattern);

		if (test_count == 0) {
			printf_safe("No test sources found.\n");
			manifest_free(m);
			sdsfree(project_dir);
			return 0;
		}
	}

	/* Determine project sources */
	glob_t proj_src;
	sds    src_pattern = sdscatprintf(sdsempty(), "%s/src/*.c", project_dir);
	i64    ret         = glob(src_pattern, 0, nullptr, &proj_src);
	sdsfree(src_pattern);

	if (ret != 0 || proj_src.gl_pathc == 0) {
		fprintf_safe(stderr, "Error: No project sources found in src/*.c\n");
		if (ret == 0) {
			globfree(&proj_src);
		}
		if (own_srcs) {
			for (size_t i = 0; i < test_count; i++) {
				sdsfree(test_srcs[i]);
			}
			free(test_srcs);
		}
		manifest_free(m);
		sdsfree(project_dir);
		return 1;
	}

	/* Build output path */
	const char *out_dir = opts->target_dir ? opts->target_dir : "target/debug";
	sds         output  = sdscatprintf(sdsempty(), "%s/%s-tests", out_dir, m->package.name);

	/* Ensure output directory exists */
	sds mkdir_cmd = sdscatprintf(sdsempty(), "mkdir -p '%s'", out_dir);
	i64 mk_ret    = system(mkdir_cmd);
	sdsfree(mkdir_cmd);
	if (mk_ret != 0) {
		fprintf_safe(stderr, "Error: Could not create output directory %s\n", out_dir);
		globfree(&proj_src);
		if (own_srcs) {
			for (size_t i = 0; i < test_count; i++) {
				sdsfree(test_srcs[i]);
			}
			free(test_srcs);
		}
		manifest_free(m);
		sdsfree(project_dir);
		sdsfree(output);
		return 1;
	}

	/* Build compiler flags */
	sds flags = sdsnew("-O0 -g -DCOFFEE_TEST_RUNNER");
	flags     = sdscatprintf(flags, " -I%s/tests", project_dir);
	flags     = sdscatprintf(flags, " -I%s/src", project_dir);

	/* Resolve dependency flags using lockfile or filesystem */
	sds         lockfile_path = sdsnew("Coffee.lock");
	lockfile_t *lock          = lockfile_parse(lockfile_path);

	for (size_t i = 0; i < m->package.dependencies_count; i++) {
		sds dep_name = dep_parse_name(m->package.dependencies[i]);
		sds dep_dir  = nullptr;

		if (lock != nullptr) {
			lockfile_dep_t *ldep = lockfile_find_dep(lock, dep_name);
			if (ldep != nullptr && ldep->path != nullptr) {
				dep_dir = sdsnew(ldep->path);
			}
		}
		if (dep_dir == nullptr) {
			dep_dir = dep_resolve_dir(dep_name);
		}
		if (dep_dir) {
			dep_add_flags(dep_dir, dep_name, &flags, nullptr, nullptr);
			sdsfree(dep_dir);
		}
		sdsfree(dep_name);
	}

	/* Build the compiler argument array: project sources + test sources */
	char  *cc        = getenv("CC") != nullptr ? getenv("CC") : "clang";
	size_t total_src = (size_t)proj_src.gl_pathc + test_count;
	sds   *all_srcs  = calloc(total_src, sizeof(sds));
	size_t src_idx   = 0;

	for (size_t i = 0; i < (size_t)proj_src.gl_pathc; i++, src_idx++) {
		all_srcs[src_idx] = sdsnew(proj_src.gl_pathv[i]);
	}
	for (size_t i = 0; i < test_count; i++, src_idx++) {
		all_srcs[src_idx] = sdsdup(test_srcs[i]);
	}

	ret = compile_sources(all_srcs, total_src, output, cc, flags, (bool)(opts->verbose || opts->verbose2));

	for (size_t i = 0; i < total_src; i++) {
		sdsfree(all_srcs[i]);
	}
	free(all_srcs);
	sdsfree(flags);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Failed to compile test runner\n");
		lockfile_free(lock);
		sdsfree(lockfile_path);
		globfree(&proj_src);
		if (own_srcs) {
			for (size_t i = 0; i < test_count; i++) {
				sdsfree(test_srcs[i]);
			}
			free(test_srcs);
		}
		manifest_free(m);
		sdsfree(project_dir);
		sdsfree(output);
		return 1;
	}

	lockfile_free(lock);
	sdsfree(lockfile_path);
	globfree(&proj_src);
	if (own_srcs) {
		for (size_t i = 0; i < test_count; i++) {
			sdsfree(test_srcs[i]);
		}
		free(test_srcs);
	}

	/* Run the test binary */
	sds         run_cmd     = sdsnew(output);
	const char *test_filter = getenv("TEST_FILTER");
	if (!test_filter && opts->inputs_num > 1) {
		test_filter = opts->inputs[1];
	}
	if (test_filter != nullptr && test_filter[0] != '\0') {
		run_cmd = sdscatprintf(run_cmd, " '%s'", test_filter);
	}
	if (opts->verbose) {
		printf_safe("Running: %s\n", run_cmd);
	}

	ret = system(run_cmd);

	sdsfree(run_cmd);

	manifest_free(m);
	sdsfree(project_dir);
	sdsfree(output);

	if (WIFEXITED(ret)) {
		return (int64_t)WEXITSTATUS(ret);
	}
	return 1;
}
