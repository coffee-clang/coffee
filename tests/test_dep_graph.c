/*
 * Unit tests for dep_graph.c — transitive dependency graph resolution.
 *
 * Covers:
 *   - Graph construction (null manifest, empty, single/multi dep)
 *   - Transitive dependency chains and diamond deduplication
 *   - Self-reference and cycle safety
 *   - Duplicate detection
 *   - All query functions (node_name, names, count, path, flags, sources, is_git, etc.)
 *   - Null/error path handling
 *   - Lockfile integration and offline mode
 */

#include "../src/build.h"
#include "../src/coffee.h"
#include "../src/dep_graph.h"
#include "../src/lockfile.h"
#include "../src/manifest.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_dep_graph_tests(void);

static char saved_cwd[4096];

static void setup_tmpdir(const char *name)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/depgraph-%s", name);
	mkdir(tmpdir, 0755);
	assert(getcwd(saved_cwd, sizeof(saved_cwd)) != nullptr);
	assert(chdir(tmpdir) == 0);
	sdsfree(tmpdir);
}

static void teardown_tmpdir(const char *name)
{
	chdir(saved_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf /tmp/depgraph-%s", name);
	system(cmd);
	sdsfree(cmd);
}

static void write_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");
	assert(fp);
	fprintf_safe(fp, "%s", content);
	fclose(fp);
}

static void write_manifest(const char *name, const char *deps_str)
{
	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	if (deps_str) {
		fprintf_safe(fp, "%s", deps_str);
	}
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"%s\"\n", name);
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);
}

static void create_dep(const char *dep_name, const char *transitive_deps_str)
{
	sds dep_dir = sdscatprintf(sdsempty(), "deps/%s", dep_name);
	mkdir(dep_dir, 0755);

	sds src_dir = sdscatprintf(sdsempty(), "deps/%s/src", dep_name);
	mkdir(src_dir, 0755);

	sds src_file    = sdscatprintf(sdsempty(), "deps/%s/src/%s.c", dep_name, dep_name);
	sds src_content = sdscatprintf(sdsempty(), "int %s_do(void) { return 0; }\n", dep_name);
	write_file(src_file, src_content);

	sds lt_path    = sdscatprintf(sdsempty(), "deps/%s/library.toml", dep_name);
	sds lt_content = sdscatprintf(sdsempty(),
	                              "[package]\n"
	                              "name = \"%s\"\n"
	                              "version = \"1.0.0\"\n",
	                              dep_name);
	if (transitive_deps_str) {
		lt_content = sdscat(lt_content, transitive_deps_str);
	}
	write_file(lt_path, lt_content);

	sdsfree(lt_content);
	sdsfree(lt_path);
	sdsfree(src_content);
	sdsfree(src_file);
	sdsfree(src_dir);
	sdsfree(dep_dir);
}

static void create_dep_ver(const char *dep_name, const char *version, const char *transitive_deps_str)
{
	sds dep_dir = sdscatprintf(sdsempty(), "deps/%s", dep_name);
	mkdir(dep_dir, 0755);

	sds src_dir = sdscatprintf(sdsempty(), "deps/%s/src", dep_name);
	mkdir(src_dir, 0755);

	sds src_file    = sdscatprintf(sdsempty(), "deps/%s/src/%s.c", dep_name, dep_name);
	sds src_content = sdscatprintf(sdsempty(), "int %s_do(void) { return 0; }\n", dep_name);
	write_file(src_file, src_content);

	sds lt_path    = sdscatprintf(sdsempty(), "deps/%s/library.toml", dep_name);
	sds lt_content = sdscatprintf(sdsempty(),
	                              "[package]\n"
	                              "name = \"%s\"\n"
	                              "version = \"%s\"\n",
	                              dep_name, version);
	if (transitive_deps_str) {
		lt_content = sdscat(lt_content, transitive_deps_str);
	}
	write_file(lt_path, lt_content);

	sdsfree(lt_content);
	sdsfree(lt_path);
	sdsfree(src_content);
	sdsfree(src_file);
	sdsfree(src_dir);
	sdsfree(dep_dir);
}

/* ===================== NULL / EMPTY MANIFEST ===================== */

TEST(dep_graph_null_manifest)
{
	dep_graph_t *g = dep_graph_create(nullptr, nullptr, false);
	ASSERT(g == nullptr, "null manifest returns null graph");
	PASS();
}

TEST(dep_graph_empty_manifest)
{
	setup_tmpdir("empty");
	mkdir("src", 0755);
	write_manifest("empty", nullptr);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 1, "only root node");

	const char *root_name = dep_graph_node_name(g, 0);
	ASSERT(root_name != nullptr, "root name not null");
	ASSERT(strcmp(root_name, "empty") == 0, "root name is package name");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("empty");
	PASS();
}

/* ===================== SINGLE DEPENDENCY ===================== */

TEST(dep_graph_single_dep)
{
	setup_tmpdir("singledep");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libfoo", nullptr);
	write_manifest("singledep", "dependencies = [\"libfoo\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 2, "root + one dep");

	ASSERT(strcmp(dep_graph_node_name(g, 0), "singledep") == 0, "index 0 is root");
	ASSERT(strcmp(dep_graph_node_name(g, 1), "libfoo") == 0, "index 1 is dep");

	const char *path = dep_graph_path(g, "libfoo");
	ASSERT(path != nullptr, "dep path resolved");

	sds flags = dep_graph_flags(g, "libfoo");
	ASSERT(flags != nullptr, "flags not null");
	sdsfree(flags);

	ASSERT(!dep_graph_is_git(g, "libfoo"), "local dep not git");
	ASSERT(dep_graph_git_ref(g, "libfoo") == nullptr, "no git ref");
	ASSERT(dep_graph_commit(g, "libfoo") == nullptr, "no commit");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("singledep");
	PASS();
}

/* ===================== MULTIPLE DEPENDENCIES ===================== */

TEST(dep_graph_multiple_deps)
{
	setup_tmpdir("multidep");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libA", nullptr);
	create_dep("libB", nullptr);
	create_dep("libC", nullptr);
	write_manifest("multidep", "dependencies = [\"libA\", \"libB\", \"libC\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 4, "root + 3 deps");

	size_t count = 0;
	sds   *names = dep_graph_names(g, &count);
	ASSERT(names != nullptr, "names array");
	ASSERT(count == 4, "names count correct");

	bool found_a = false, found_b = false, found_c = false;
	for (size_t i = 0; i < count; i++) {
		if (strcmp(names[i], "libA") == 0) {
			found_a = true;
		}
		if (strcmp(names[i], "libB") == 0) {
			found_b = true;
		}
		if (strcmp(names[i], "libC") == 0) {
			found_c = true;
		}
	}
	ASSERT(found_a && found_b && found_c, "all deps in names array");
	free(names);

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("multidep");
	PASS();
}

/* ===================== TRANSITIVE CHAIN ===================== */

TEST(dep_graph_transitive_chain)
{
	setup_tmpdir("chain");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	create_dep("libC", nullptr);
	create_dep("libB", "[dependencies]\nlibC = \"1.0.0\"\n");
	create_dep("libA", "[dependencies]\nlibB = \"1.0.0\"\n");
	write_manifest("chain", "dependencies = [\"libA\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* root + libA + libB + libC = 4 nodes */
	ASSERT(dep_graph_count(g) == 4, "root + 3 transitive deps");

	/* Verify all transitive deps are present */
	const char *path_a = dep_graph_path(g, "libA");
	const char *path_b = dep_graph_path(g, "libB");
	const char *path_c = dep_graph_path(g, "libC");
	ASSERT(path_a != nullptr, "libA path resolved");
	ASSERT(path_b != nullptr, "libB path resolved");
	ASSERT(path_c != nullptr, "libC path resolved");

	/* Verify flags for all levels */
	sds flags_a = dep_graph_flags(g, "libA");
	sds flags_b = dep_graph_flags(g, "libB");
	sds flags_c = dep_graph_flags(g, "libC");
	ASSERT(sdslen(flags_a) > 0, "libA has flags");
	ASSERT(sdslen(flags_b) > 0, "libB has flags");
	ASSERT(sdslen(flags_c) > 0, "libC has flags");
	sdsfree(flags_a);
	sdsfree(flags_b);
	sdsfree(flags_c);

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("chain");
	PASS();
}

/* ===================== DIAMOND DEPENDENCY (DEDUPLICATION) ===================== */

TEST(dep_graph_diamond_dedup)
{
	setup_tmpdir("diamond");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	create_dep("libC", nullptr);
	create_dep("libA", "[dependencies]\nlibC = \"1.0.0\"\n");
	create_dep("libB", "[dependencies]\nlibC = \"1.0.0\"\n");
	write_manifest("diamond", "dependencies = [\"libA\", \"libB\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* root + libA + libB + libC = 4 (NOT 5 — libC appears only once) */
	ASSERT(dep_graph_count(g) == 4, "diamond deduplicates shared dep");

	/* libC is present exactly once */
	const char *path_c = dep_graph_path(g, "libC");
	ASSERT(path_c != nullptr, "libC present");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("diamond");
	PASS();
}

/* ===================== SELF-REFERENCE SKIP ===================== */

TEST(dep_graph_self_reference)
{
	setup_tmpdir("selfref");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	/* libSelf lists itself as a dependency */
	create_dep("libSelf", "[dependencies]\nlibSelf = \"1.0.0\"\n");
	write_manifest("selfref", "dependencies = [\"libSelf\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* Should have root + libSelf = 2, not infinite */
	ASSERT(dep_graph_count(g) == 2, "self-reference skipped");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("selfref");
	PASS();
}

/* ===================== DUPLICATE ROOT DEP ===================== */

TEST(dep_graph_duplicate_root_dep)
{
	setup_tmpdir("dupdep");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libDup", nullptr);
	write_manifest("dupdep", "dependencies = [\"libDup\", \"libDup\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	ASSERT(dep_graph_count(g) == 2, "duplicate dep added only once");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("dupdep");
	PASS();
}

/* ===================== CYCLE SAFETY (A→B, B→A) ===================== */

TEST(dep_graph_cycle_safety)
{
	setup_tmpdir("cycle");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	create_dep("libB", "[dependencies]\nlibA = \"1.0.0\"\n");
	create_dep("libA", "[dependencies]\nlibB = \"1.0.0\"\n");
	write_manifest("cycle", "dependencies = [\"libA\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* root + libA + libB = 3. libA→libB, libB→libA (already seen, skip) */
	ASSERT(dep_graph_count(g) == 3, "cycle detected and broken");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("cycle");
	PASS();
}

/* ===================== NODE_NAME OUT OF BOUNDS ===================== */

TEST(dep_graph_node_name_oob)
{
	setup_tmpdir("oob");
	mkdir("src", 0755);
	write_manifest("oob", nullptr);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	ASSERT(dep_graph_node_name(g, 999) == nullptr, "out of bounds returns null");
	ASSERT(dep_graph_node_name(g, 1) == nullptr, "beyond last index returns null");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("oob");
	PASS();
}

/* ===================== SOURCES COLLECTION ===================== */

TEST(dep_graph_sources)
{
	setup_tmpdir("sources");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libSrc", nullptr);
	write_manifest("sources", "dependencies = [\"libSrc\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	size_t     src_count = 0;
	const sds *srcs      = dep_graph_sources(g, "libSrc", &src_count);
	ASSERT(srcs != nullptr, "sources not null");
	ASSERT(src_count >= 1, "at least one source file");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("sources");
	PASS();
}

/* ===================== NULL ARGUMENT HANDLING ===================== */

TEST(dep_graph_null_args)
{
	setup_tmpdir("nullargs");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libN", nullptr);
	write_manifest("nullargs", "dependencies = [\"libN\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* dep_graph_count with null */
	ASSERT(dep_graph_count(nullptr) == 0, "null graph count = 0");

	/* dep_graph_node_name with null graph */
	ASSERT(dep_graph_node_name(nullptr, 0) == nullptr, "null graph node_name");

	/* dep_graph_names with null graph */
	size_t c = 99;
	ASSERT(dep_graph_names(nullptr, &c) == nullptr, "null graph names");
	ASSERT(c == 99, "count unchanged on null graph");

	/* dep_graph_names with null count pointer */
	ASSERT(dep_graph_names(g, nullptr) == nullptr, "null count names");

	/* dep_graph_flags with null args */
	sds f = dep_graph_flags(nullptr, "libN");
	ASSERT(f != nullptr && sdslen(f) == 0, "null graph flags empty");
	sdsfree(f);

	f = dep_graph_flags(g, nullptr);
	ASSERT(f != nullptr && sdslen(f) == 0, "null name flags empty");
	sdsfree(f);

	/* dep_graph_sources with null args */
	size_t sc = 99;
	ASSERT(dep_graph_sources(nullptr, "libN", &sc) == nullptr, "null graph sources");
	ASSERT(sc == 99, "count unchanged on null graph sources");

	/* dep_graph_path with null args */
	ASSERT(dep_graph_path(nullptr, "libN") == nullptr, "null graph path");
	ASSERT(dep_graph_path(g, nullptr) == nullptr, "null name path");

	/* dep_graph_is_git with null args */
	ASSERT(!dep_graph_is_git(nullptr, "libN"), "null graph is_git");

	/* dep_graph_git_ref with null args */
	ASSERT(dep_graph_git_ref(nullptr, "libN") == nullptr, "null graph git_ref");

	/* dep_graph_commit with null args */
	ASSERT(dep_graph_commit(nullptr, "libN") == nullptr, "null graph commit");

	/* dep_graph_compare_remote with null args */
	ASSERT(dep_graph_compare_remote(nullptr, "libN", nullptr) == -1, "null graph compare_remote");

	/* dep_graph_fetch_git with null args */
	ASSERT(dep_graph_fetch_git(nullptr, "libN", false) == -1, "null graph fetch_git");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("nullargs");
	PASS();
}

/* ===================== NONEXISTENT DEP QUERIES ===================== */

TEST(dep_graph_nonexistent_dep)
{
	setup_tmpdir("nexist");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libReal", nullptr);
	write_manifest("nexist", "dependencies = [\"libReal\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	ASSERT(dep_graph_path(g, "nonexistent") == nullptr, "nonexistent path null");
	ASSERT(!dep_graph_is_git(g, "ghost"), "nonexistent not git");

	sds f = dep_graph_flags(g, "nope");
	ASSERT(f != nullptr && sdslen(f) == 0, "nonexistent flags empty");
	sdsfree(f);

	size_t sc = 99;
	ASSERT(dep_graph_sources(g, "no_such_dep", &sc) == nullptr, "nonexistent sources null");
	ASSERT(sc == 0, "count 0");

	ASSERT(dep_graph_commit(g, "phantom") == nullptr, "nonexistent commit null");
	ASSERT(dep_graph_git_ref(g, "missing") == nullptr, "nonexistent git_ref null");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("nexist");
	PASS();
}

/* ===================== OFFLINE MODE ===================== */

TEST(dep_graph_offline_mode)
{
	setup_tmpdir("offline");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libOff", nullptr);
	write_manifest("offline", "dependencies = [\"libOff\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, true);
	ASSERT(g != nullptr, "offline graph created");
	ASSERT(dep_graph_count(g) == 2, "root + dep in offline mode");

	/* compare_remote in offline returns -1 */
	sds behind = nullptr;
	i64 ret    = dep_graph_compare_remote(g, "libOff", &behind);
	ASSERT(ret == -1, "offline compare_remote returns -1");
	if (behind) {
		ASSERT(strcmp(behind, "(offline)") == 0, "offline message");
		sdsfree(behind);
	}

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("offline");
	PASS();
}

/* ===================== LOCKFILE INTEGRATION ===================== */

TEST(dep_graph_with_lockfile)
{
	setup_tmpdir("lockint");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libLock", nullptr);
	write_manifest("lockint", "dependencies = [\"libLock\"]\n");

	/* Create lockfile with explicit path */
	char cwd_buf[4096];
	ASSERT(getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr, "getcwd");
	sds lockpath = sdscatprintf(sdsempty(), "%s/deps/libLock", cwd_buf);

	FILE *lf = fopen("Coffee.lock", "w");
	ASSERT(lf != nullptr, "create Coffee.lock");
	fprintf_safe(lf, "[[dependency]]\n");
	fprintf_safe(lf, "name = \"libLock\"\n");
	fprintf_safe(lf, "version = \"1.0.0\"\n");
	fprintf_safe(lf, "path = \"%s\"\n", lockpath);
	fclose(lf);
	sdsfree(lockpath);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	lockfile_t *lock = lockfile_parse("Coffee.lock");
	ASSERT(lock != nullptr, "parse lockfile");

	dep_graph_t *g = dep_graph_create(m, lock, false);
	ASSERT(g != nullptr, "graph with lockfile");

	ASSERT(dep_graph_count(g) == 2, "root + dep via lockfile");

	const char *path = dep_graph_path(g, "libLock");
	ASSERT(path != nullptr, "path from lockfile");

	dep_graph_free(g);
	lockfile_free(lock);
	manifest_free(m);
	teardown_tmpdir("lockint");
	PASS();
}

/* ===================== DEEP TRANSITIVE (4 levels) ===================== */

TEST(dep_graph_deep_transitive)
{
	setup_tmpdir("deep");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	create_dep("libD", nullptr);
	create_dep("libC", "[dependencies]\nlibD = \"1.0.0\"\n");
	create_dep("libB", "[dependencies]\nlibC = \"1.0.0\"\n");
	create_dep("libA", "[dependencies]\nlibB = \"1.0.0\"\n");
	write_manifest("deep", "dependencies = [\"libA\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* root + A + B + C + D = 5 */
	ASSERT(dep_graph_count(g) == 5, "deep chain all resolved");

	ASSERT(dep_graph_path(g, "libD") != nullptr, "deepest dep resolved");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("deep");
	PASS();
}

/* ===================== ROOT NODE PROPERTIES ===================== */

TEST(dep_graph_root_properties)
{
	setup_tmpdir("rootprop");
	mkdir("src", 0755);
	write_manifest("rootprop", nullptr);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	ASSERT(strcmp(dep_graph_node_name(g, 0), "rootprop") == 0, "root name");
	ASSERT(strcmp(dep_graph_path(g, "rootprop"), ".") == 0, "root path is .");
	ASSERT(!dep_graph_is_git(g, "rootprop"), "root not git");

	const char *ver = g->nodes[0].version;
	ASSERT(ver != nullptr && strcmp(ver, "1.0.0") == 0, "root version");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("rootprop");
	PASS();
}

/* ===================== DEP WITHOUT LIBRARY.TOML (missing dir) ===================== */

TEST(dep_graph_missing_dep_dir)
{
	setup_tmpdir("missing");
	mkdir("src", 0755);
	write_manifest("missing", "dependencies = [\"ghostdep\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created even with missing dep");

	/* ghostdep is in the graph, but path is null */
	ASSERT(dep_graph_count(g) == 2, "root + ghost dep");

	const char *path = dep_graph_path(g, "ghostdep");
	/* path may be null if unresolvable */
	ASSERT(path == nullptr, "unresolvable dep has no path");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("missing");
	PASS();
}

/* ===================== MULTIPLE TRANSITIVE FROM SAME SOURCE ===================== */

TEST(dep_graph_multiple_transitive)
{
	setup_tmpdir("multitrans");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	create_dep("libX", nullptr);
	create_dep("libY", nullptr);
	create_dep("libHub", "[dependencies]\nlibX = \"1.0.0\"\nlibY = \"1.0.0\"\n");
	write_manifest("multitrans", "dependencies = [\"libHub\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* root + libHub + libX + libY = 4 */
	ASSERT(dep_graph_count(g) == 4, "all transitive deps from hub");

	ASSERT(dep_graph_path(g, "libX") != nullptr, "libX resolved");
	ASSERT(dep_graph_path(g, "libY") != nullptr, "libY resolved");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("multitrans");
	PASS();
}

/* ===================== VERSION CONSTRAINT FIELD ===================== */

TEST(dep_graph_version_constraint_field)
{
	setup_tmpdir("vconstraint");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libCstr", nullptr);
	write_manifest("vconstraint", "dependencies = [\"libCstr = \\\">= 2.0\\\"\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 2, "root + dep");

	/* version_constraint should be set from the raw dependency string */
	ASSERT(g->nodes[1].version_constraint != nullptr, "constraint field set");
	ASSERT(strcmp(g->nodes[1].version_constraint, ">= 2.0") == 0, "constraint value correct");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("vconstraint");
	PASS();
}

/* ===================== TRANSITIVE CONSTRAINT SATISFIED ===================== */

TEST(dep_graph_transitive_constraint_satisfied)
{
	setup_tmpdir("tcsat");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	/* libB version 2.0.0 — satisfies >= 1.0 constraint from libA */
	create_dep_ver("libB", "2.0.0", nullptr);
	create_dep("libA", "[dependencies]\nlibB = \">= 1.0.0\"\n");
	write_manifest("tcsat", "dependencies = [\"libA\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 3, "root + libA + libB");

	/* libB has version_constraint from libA */
	bool found_lib_b = false;
	for (size_t j = 0; j < g->count; j++) {
		if (g->nodes[j].name != nullptr && strcmp(g->nodes[j].name, "libB") == 0) {
			found_lib_b = true;
			ASSERT(g->nodes[j].version_constraint != nullptr, "libB has constraint");
			ASSERT(strcmp(g->nodes[j].version_constraint, ">= 1.0.0") == 0, "constraint correct");
			break;
		}
	}
	ASSERT(found_lib_b, "libB found in graph");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("tcsat");
	PASS();
}

/* ===================== TRANSITIVE CONSTRAINT UNSATISFIED ===================== */

TEST(dep_graph_transitive_constraint_unsatisfied)
{
	setup_tmpdir("tcunsat");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	/* libB version 1.0.0 — does NOT satisfy >= 2.0 constraint from libA */
	create_dep_ver("libB", "1.0.0", nullptr);
	create_dep("libA", "[dependencies]\nlibB = \">= 2.0.0\"\n");
	write_manifest("tcunsat", "dependencies = [\"libA\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created even with unsatisfied constraint");
	ASSERT(dep_graph_count(g) == 3, "all deps still in graph (warning only)");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("tcunsat");
	PASS();
}

/* ===================== ROOT CONSTRAINT FROM RAW STRING ===================== */

TEST(dep_graph_root_constraint)
{
	setup_tmpdir("rcsat");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	/* libUse version 1.5.0 — satisfies == 1.5.0 constraint */
	create_dep_ver("libUse", "1.5.0", nullptr);
	write_manifest("rcsat", "dependencies = [\"libUse = \\\"== 1.5.0\\\"\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 2, "root + dep");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("rcsat");
	PASS();
}

/* ===================== DIAMOND WITH MIXED CONSTRAINTS ===================== */

TEST(dep_graph_constraint_diamond)
{
	setup_tmpdir("cdiamond");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	/* libShared version 2.3.0 */
	create_dep_ver("libShared", "2.3.0", nullptr);
	/* libTop requires libShared >= 2.0 */
	create_dep("libTop", "[dependencies]\nlibShared = \">= 2.0.0\"\n");
	/* libSide requires libShared >= 2.0, <= 2.4 */
	create_dep("libSide", "[dependencies]\nlibShared = \">= 2.0.0, <= 2.4.0\"\n");
	write_manifest("cdiamond", "dependencies = [\"libTop\", \"libSide\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");

	/* root + libTop + libSide + libShared = 4 (deduplicated) */
	ASSERT(dep_graph_count(g) == 4, "diamond deduplicated with constraints");

	/* libShared is present */
	ASSERT(dep_graph_path(g, "libShared") != nullptr, "libShared resolved");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("cdiamond");
	PASS();
}

/* ===================== WILDCARD CONSTRAINT ===================== */

TEST(dep_graph_constraint_wildcard)
{
	setup_tmpdir("cwild");
	mkdir("src", 0755);
	mkdir("deps", 0755);

	create_dep_ver("libW", "0.0.1", nullptr);
	create_dep("libA", "[dependencies]\nlibW = \"*\"\n");
	write_manifest("cwild", "dependencies = [\"libA\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 3, "root + libA + libW (wildcard always satisfied)");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("cwild");
	PASS();
}

/* ===================== CACHE TESTS ===================== */

TEST(dep_graph_cache_hit)
{
	setup_tmpdir("cachehit");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libFoo", nullptr);
	write_manifest("cachehit", "dependencies = [\"libFoo\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	/* First call: builds from scratch, writes cache */
	dep_graph_t *g1 = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g1 != nullptr, "first get succeeds");
	ASSERT(dep_graph_count(g1) == 2, "root + libFoo");

	/* Second call: should hit cache */
	dep_graph_t *g2 = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g2 != nullptr, "second get succeeds");
	ASSERT(dep_graph_count(g2) == 2, "same node count from cache");

	ASSERT(dep_graph_path(g2, "libFoo") != nullptr, "cached path resolved");

	dep_graph_free(g2);
	dep_graph_free(g1);
	manifest_free(m);
	teardown_tmpdir("cachehit");
	PASS();
}

TEST(dep_graph_cache_stale_toml)
{
	setup_tmpdir("cachestale");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libBar", nullptr);
	write_manifest("cachestale", "dependencies = [\"libBar\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	/* Build graph and cache it */
	dep_graph_t *g1 = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g1 != nullptr, "first get succeeds");
	ASSERT(dep_graph_count(g1) == 2, "root + libBar");
	dep_graph_free(g1);

	/* Touch Coffee.toml to invalidate cache */
	sleep(1);
	FILE *fp = fopen("Coffee.toml", "a");
	ASSERT(fp != nullptr, "reopen Coffee.toml");
	fprintf_safe(fp, "\n");
	fclose(fp);

	/* Second call: should miss cache, rebuild */
	dep_graph_t *g2 = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g2 != nullptr, "second get succeeds after invalidation");
	ASSERT(dep_graph_count(g2) == 2, "same count after rebuild");

	dep_graph_free(g2);
	manifest_free(m);
	teardown_tmpdir("cachestale");
	PASS();
}

TEST(dep_graph_get_null_project_dir)
{
	setup_tmpdir("nulldir");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libNull", nullptr);
	write_manifest("nulldir", "dependencies = [\"libNull\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	/* Null project_dir: falls back to dep_graph_create, no caching */
	dep_graph_t *g = dep_graph_get(m, nullptr, true, nullptr);
	ASSERT(g != nullptr, "graph created without caching");
	ASSERT(dep_graph_count(g) == 2, "root + libNull");

	dep_graph_free(g);
	manifest_free(m);
	teardown_tmpdir("nulldir");
	PASS();
}

TEST(dep_graph_cache_corrupt_recovery)
{
	setup_tmpdir("cachecorrupt");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libCorrupt", nullptr);
	write_manifest("cachecorrupt", "dependencies = [\"libCorrupt\"]\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	/* Build graph and cache it */
	dep_graph_t *g1 = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g1 != nullptr, "first get succeeds");
	dep_graph_free(g1);

	/* Corrupt the cache file */
	sds   cache_path = sdscatprintf(sdsempty(), ".coffee/build-cache/cachecorrupt.graph");
	FILE *cfp        = fopen(cache_path, "w");
	ASSERT(cfp != nullptr, "open cache for corruption");
	fprintf_safe(cfp, "this is not valid toml {{{");
	fclose(cfp);
	sdsfree(cache_path);

	/* Second call: should recover from corrupt cache */
	dep_graph_t *g2 = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g2 != nullptr, "recovers from corrupt cache");
	ASSERT(dep_graph_count(g2) == 2, "correct count after recovery");

	dep_graph_free(g2);
	manifest_free(m);
	teardown_tmpdir("cachecorrupt");
	PASS();
}

TEST(dep_graph_cache_with_lockfile)
{
	setup_tmpdir("cachelock");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libLocked", nullptr);
	write_manifest("cachelock", "dependencies = [\"libLocked\"]\n");

	char cwd_buf[4096];
	ASSERT(getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr, "getcwd");
	sds lockpath = sdscatprintf(sdsempty(), "%s/deps/libLocked", cwd_buf);

	FILE *lfp = fopen("Coffee.lock", "w");
	ASSERT(lfp != nullptr, "create Coffee.lock");
	fprintf_safe(lfp, "[[dependency]]\n");
	fprintf_safe(lfp, "name = \"libLocked\"\n");
	fprintf_safe(lfp, "version = \"1.0.0\"\n");
	fprintf_safe(lfp, "path = \"%s\"\n", lockpath);
	fclose(lfp);
	sdsfree(lockpath);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	lockfile_t *lock = lockfile_parse("Coffee.lock");
	ASSERT(lock != nullptr, "parse lockfile");

	/* Build with lockfile */
	dep_graph_t *g1 = dep_graph_get(m, lock, true, ".");
	ASSERT(g1 != nullptr, "first get with lockfile");
	ASSERT(dep_graph_count(g1) == 2, "root + dep via lockfile");
	const char *path1 = dep_graph_path(g1, "libLocked");
	ASSERT(path1 != nullptr, "path from lockfile in g1");

	/* Second get: should hit cache */
	dep_graph_t *g2 = dep_graph_get(m, lock, true, ".");
	ASSERT(g2 != nullptr, "cache hit with lockfile");
	const char *path2 = dep_graph_path(g2, "libLocked");
	ASSERT(path2 != nullptr, "path from lockfile in g2");
	ASSERT(strcmp(path1, path2) == 0, "same path from cache");

	dep_graph_free(g2);
	dep_graph_free(g1);
	lockfile_free(lock);
	manifest_free(m);
	teardown_tmpdir("cachelock");
	PASS();
}

TEST(dep_graph_cache_lockfile_mtime_change)
{
	setup_tmpdir("lockmtime");
	mkdir("src", 0755);
	mkdir("deps", 0755);
	create_dep("libMT", nullptr);
	write_manifest("lockmtime", "dependencies = [\"libMT\"]\n");

	/* Create initial lockfile */
	FILE *lfp = fopen("Coffee.lock", "w");
	ASSERT(lfp != nullptr, "create Coffee.lock");
	fprintf_safe(lfp, "[[dependency]]\nname = \"libMT\"\nversion = \"1.0.0\"\n");
	fclose(lfp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	lockfile_t *lock = lockfile_parse("Coffee.lock");
	ASSERT(lock != nullptr, "parse lockfile");

	dep_graph_t *g1 = dep_graph_get(m, lock, true, ".");
	ASSERT(g1 != nullptr, "first get");
	dep_graph_free(g1);

	/* Touch Coffee.lock to invalidate cache */
	sleep(1);
	lfp = fopen("Coffee.lock", "a");
	ASSERT(lfp != nullptr, "reopen Coffee.lock");
	fprintf_safe(lfp, "\n");
	fclose(lfp);

	dep_graph_t *g2 = dep_graph_get(m, lock, true, ".");
	ASSERT(g2 != nullptr, "rebuild after lockfile change");
	ASSERT(dep_graph_count(g2) == 2, "correct count after lockfile invalidation");

	dep_graph_free(g2);
	lockfile_free(lock);
	manifest_free(m);
	teardown_tmpdir("lockmtime");
	PASS();
}

void coffee_register_dep_graph_tests(void)
{
	TEST_REGISTER(dep_graph_null_manifest);
	TEST_REGISTER(dep_graph_empty_manifest);
	TEST_REGISTER(dep_graph_single_dep);
	TEST_REGISTER(dep_graph_multiple_deps);
	TEST_REGISTER(dep_graph_transitive_chain);
	TEST_REGISTER(dep_graph_diamond_dedup);
	TEST_REGISTER(dep_graph_self_reference);
	TEST_REGISTER(dep_graph_duplicate_root_dep);
	TEST_REGISTER(dep_graph_cycle_safety);
	TEST_REGISTER(dep_graph_node_name_oob);
	TEST_REGISTER(dep_graph_sources);
	TEST_REGISTER(dep_graph_null_args);
	TEST_REGISTER(dep_graph_nonexistent_dep);
	TEST_REGISTER(dep_graph_offline_mode);
	TEST_REGISTER(dep_graph_with_lockfile);
	TEST_REGISTER(dep_graph_deep_transitive);
	TEST_REGISTER(dep_graph_root_properties);
	TEST_REGISTER(dep_graph_missing_dep_dir);
	TEST_REGISTER(dep_graph_multiple_transitive);
	TEST_REGISTER(dep_graph_version_constraint_field);
	TEST_REGISTER(dep_graph_transitive_constraint_satisfied);
	TEST_REGISTER(dep_graph_transitive_constraint_unsatisfied);
	TEST_REGISTER(dep_graph_root_constraint);
	TEST_REGISTER(dep_graph_constraint_diamond);
	TEST_REGISTER(dep_graph_constraint_wildcard);
	TEST_REGISTER(dep_graph_cache_hit);
	TEST_REGISTER(dep_graph_cache_stale_toml);
	TEST_REGISTER(dep_graph_get_null_project_dir);
	TEST_REGISTER(dep_graph_cache_corrupt_recovery);
	TEST_REGISTER(dep_graph_cache_with_lockfile);
	TEST_REGISTER(dep_graph_cache_lockfile_mtime_change);
}
