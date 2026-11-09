#include "../build.h"
#include "../coffee.h"
#include "../dep_graph.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

#include <stdio.h>
#include <string.h>

#include <glob.h>
#include <sys/stat.h>
#include <unistd.h>

/* A growable, deduplicated list of source files. */
struct src_files {
	sds   *items;
	size_t count;
	size_t capacity;
};

static void src_files_add(struct src_files *list, const char *path)
{
	for (size_t i = 0; i < list->count; i++) {
		if (strcmp(list->items[i], path) == 0) {
			return;
		}
	}
	if (list->count == list->capacity) {
		list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
		list->items    = safe_realloc(list->items, list->capacity * sizeof(sds));
	}
	list->items[list->count++] = sdsnew(path);
}

static void src_files_free(struct src_files *list)
{
	for (size_t i = 0; i < list->count; i++) {
		sdsfree(list->items[i]);
	}
	safe_free(list->items);
}

/* Add every file matching <project_dir>/<pattern> to list. */
static void src_files_glob(struct src_files *list, const char *project_dir, const char *pattern)
{
	if (pattern == nullptr || pattern[0] == '\0') {
		return;
	}
	glob_t g;
	sds    full = sdscatprintf(sdsempty(), "%s/%s", project_dir, pattern);
	if (glob(full, 0, nullptr, &g) == 0) {
		for (size_t i = 0; i < g.gl_pathc; i++) {
			src_files_add(list, g.gl_pathv[i]);
		}
		globfree(&g);
	}
	sdsfree(full);
}

/*
 * Collect the project sources the test runner links against: [test]
 * project_sources when set, else the same sources the generated Makefile
 * builds.  On failure, prints the patterns that matched nothing.
 */
static bool collect_project_sources(const manifest_t *m, const char *project_dir, struct src_files *list)
{
	sds   *patterns = nullptr;
	size_t count    = 0;
	if (m->test.project_sources_count > 0) {
		patterns = m->test.project_sources;
		count    = m->test.project_sources_count;
	}
	sds *fallback = nullptr;
	if (count == 0) {
		fallback = manifest_project_sources(m, &count);
		patterns = fallback;
	}
	for (size_t i = 0; i < count; i++) {
		src_files_glob(list, project_dir, patterns[i]);
	}
	if (list->count == 0) {
		sds joined = sdsempty();
		for (size_t i = 0; i < count; i++) {
			joined = sdscatprintf(joined, "%s%s", i > 0 ? ", " : "", patterns[i] != nullptr ? patterns[i] : "");
		}
		fprintf_safe(stderr, "Error: No project sources found in %s\n", joined);
		sdsfree(joined);
	}
	for (size_t i = 0; fallback != nullptr && i < count; i++) {
		sdsfree(fallback[i]);
	}
	safe_free(fallback);
	return list->count > 0;
}

/* Collect the test sources: the [test] sources globs, else every .c file in tests/. */
static void collect_test_sources(const manifest_t *m, const char *project_dir, struct src_files *list)
{
	if (m->test.sources_count == 0) {
		src_files_glob(list, project_dir, "tests/*.c");
		return;
	}
	for (size_t i = 0; i < m->test.sources_count; i++) {
		src_files_glob(list, project_dir, m->test.sources[i]);
	}
}

/*
 * Add the dependency flags and sources the generated Makefile uses: the
 * compiler flags to flags, -L/-l of prebuilt deps to link, and the
 * sources of deps that ship them to srcs.
 */
static void add_dependency_flags(manifest_t *m, options *opts, sds *flags, sds *link, struct src_files *srcs)
{
	lockfile_t  *lock = lockfile_parse("Coffee.lock");
	dep_graph_t *dg   = dep_graph_create(m, lock, opts->offline);
	lockfile_free(lock);
	if (dg == nullptr) {
		fprintf_safe(stderr, "Warning: Could not resolve dependency graph\n");
		fprintf_safe(stderr, "  Continuing without dependency flags\n");
		return;
	}
	/* Skip the root at index 0 */
	for (size_t i = 1; i < dep_graph_count(dg); i++) {
		const char *dep_name = dep_graph_node_name(dg, i);
		if (dep_name == nullptr) {
			continue;
		}
		size_t     src_count = 0;
		const sds *dep_srcs  = dep_graph_sources(dg, dep_name, &src_count);
		for (size_t s = 0; s < src_count; s++) {
			src_files_add(srcs, dep_srcs[s]);
		}
		sds dep_flags = dep_graph_flags(dg, dep_name);
		dep_split_flags(dep_flags, src_count > 0, flags, link);
		sdsfree(dep_flags);
	}
	dep_graph_free(dg);
}

/* Run the test binary with the filter from TEST_FILTER or the first argument. */
static i64 run_test_binary(options *opts, sds output)
{
	char *test_filter = getenv("TEST_FILTER");
	if (!test_filter && opts->inputs_num > 1) {
		test_filter = opts->inputs[1];
	}

	if (opts->verbose) {
		printf_safe("Running: %s", output);
		if (test_filter && test_filter[0]) {
			printf_safe(" %s", test_filter);
		}
		printf_safe("\n");
	}

	char *filter_arg  = (test_filter && test_filter[0]) ? test_filter : nullptr;
	char *run_argv[4] = { output, filter_arg, nullptr, nullptr };
	/* run_command() already reduced the wait status to an exit status */
	return run_command(run_argv, 0);
}

/* Compile the project, its dependencies and the tests into one runner. */
static i64 build_and_run(manifest_t *m, options *opts, const char *project_dir, struct src_files *srcs)
{
	char *out_dir      = opts->target_dir ? opts->target_dir : (char *)"build/debug";
	char *mkdir_argv[] = { "mkdir", "-p", out_dir, nullptr };
	if (run_command(mkdir_argv, 0) != 0) {
		fprintf_safe(stderr, "Error: Could not create output directory %s\n", out_dir);
		return 1;
	}

	sds cflags  = sdsnew("-O0 -g -DCOFFEE_TEST_RUNNER");
	cflags      = sdscatprintf(cflags, " -I\"%s/tests\"", project_dir);
	cflags      = sdscatprintf(cflags, " -I\"%s/src\"", project_dir);
	sds ldflags = sdsempty();
	add_dependency_flags(m, opts, &cflags, &ldflags, srcs);

	char *cc     = getenv("CC") != nullptr ? getenv("CC") : "clang";
	sds   output = sdscatprintf(sdsempty(), "%s/%s-tests", out_dir, m->package.name);
	i64   ret =
	    compile_sources(srcs->items, srcs->count, output, cc, cflags, ldflags, (bool)(opts->verbose || opts->verbose2));
	sdsfree(ldflags);
	sdsfree(cflags);

	if (ret != 0) {
		fprintf_safe(stderr, "Error: Failed to compile test runner\n");
		ret = 1;
	} else {
		ret = run_test_binary(opts, output);
	}
	sdsfree(output);
	return ret;
}

int64_t handle_test(options *opts)
{
	/* Find and parse manifest */
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml in current directory\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		fprintf_safe(stderr, "  Check the file for syntax errors and run 'coffee check' for validation.\n");
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

	struct src_files test_srcs = { 0 };
	struct src_files srcs      = { 0 };
	i64              ret       = 1;

	/* Fetch declared dependencies that are not on disk yet. */
	if (deps_fetch_missing(m, opts) != 0) {
		goto done;
	}

	collect_test_sources(m, project_dir, &test_srcs);
	if (test_srcs.count == 0) {
		printf_safe("No test sources found.\n");
		ret = 0;
		goto done;
	}
	if (!collect_project_sources(m, project_dir, &srcs)) {
		goto done;
	}
	for (size_t i = 0; i < test_srcs.count; i++) {
		src_files_add(&srcs, test_srcs.items[i]);
	}
	ret = build_and_run(m, opts, project_dir, &srcs);

done:
	src_files_free(&srcs);
	src_files_free(&test_srcs);
	manifest_free(m);
	sdsfree(project_dir);
	return ret;
}
