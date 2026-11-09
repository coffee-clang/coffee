/*
 * Additional unit tests that raise per-function coverage (parsers).
 *
 * Covers the error and edge branches of cmdline.c, coffee_features.c,
 * makefile.c, lockfile.c, manifest.c and version.c.
 */

#include "../src/cmdline.h"
#include "../src/coffee.h"
#include "../src/coffee_features.h"
#include "../src/dep_graph.h"
#include "../src/lockfile.h"
#include "../src/makefile.h"
#include "../src/manifest.h"
#include "../src/safeio.h"
#include "../src/strings.h"
#include "../src/version.h"
#include "safe.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void coffee_register_cov_parsers_tests(void);

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)st;
	(void)type;
	(void)ftw;
	return remove(path);
}

/* Create a unique scratch directory; returns an sds (caller frees) or nullptr. */
static sds make_scratch_dir(void)
{
	sds dir = sdsnew("/tmp/coffee-covparsers-XXXXXX");
	if (mkdtemp(dir) == nullptr) {
		sdsfree(dir);
		return nullptr;
	}
	return dir;
}

/* Remove a directory tree and free the path. */
static void drop_scratch_dir(sds dir)
{
	nftw(dir, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
	sdsfree(dir);
}

/* Write content to <dir>/<name> (creating parent dirs is the caller's job).
 * Returns the full path as an sds, or nullptr on failure. */
static sds write_file(const char *dir, const char *name, const char *content)
{
	sds   path = sdscatprintf(sdsempty(), "%s/%s", dir, name);
	FILE *fp   = safe_fopen(path, "w");
	if (fp == nullptr) {
		sdsfree(path);
		return nullptr;
	}
	fputs(content, fp);
	safe_fclose(fp);
	return path;
}

static sds sub_path(const char *dir, const char *name)
{
	return sdscatprintf(sdsempty(), "%s/%s", dir, name);
}

static sds read_all(const char *path)
{
	FILE *fp = safe_fopen(path, "r");
	if (fp == nullptr) {
		return nullptr;
	}
	sds    out = sdsempty();
	char   buf[1'024];
	size_t n = fread(buf, 1, sizeof(buf), fp);
	while (n > 0) {
		out = sdscatlen(out, buf, n);
		n   = fread(buf, 1, sizeof(buf), fp);
	}
	safe_fclose(fp);
	return out;
}

/* Parse a manifest from literal TOML text placed in dir; nullptr on failure. */
static manifest_t *parse_manifest_text(const char *dir, const char *text)
{
	sds path = write_file(dir, "Coffee.toml", text);
	if (path == nullptr) {
		return nullptr;
	}
	manifest_t *m = manifest_parse(path);
	sdsfree(path);
	return m;
}

static bool dir_contains_tmp_files(const char *dir)
{
	sds  cmd_path = sdscatprintf(sdsempty(), "%s/Makefile.%d.tmp", dir, (int)getpid());
	bool exists   = access(cmd_path, F_OK) == 0;
	sdsfree(cmd_path);
	return exists;
}

/* ------------------------------------------------------------------ */
/* version.c                                                          */
/* ------------------------------------------------------------------ */

TEST(covp_version_parse_edges)
{
	version_t v;
	ASSERT(!version_parse(nullptr, &v), "null string rejected");
	ASSERT(!version_parse("1.2.3", nullptr), "null out rejected");
	ASSERT(version_parse("  1.2.3", &v), "leading space accepted");
	ASSERT(v.major == 1 && v.minor == 2 && v.patch == 3, "leading space value");
	ASSERT(!version_parse("abc", &v), "non-digit major rejected");
	ASSERT(!version_parse("", &v), "empty rejected");
	ASSERT(version_parse("1. 2. 3", &v), "space after dots accepted");
	ASSERT(v.major == 1 && v.minor == 2 && v.patch == 3, "space after dots value");
	ASSERT(!version_parse("1.x", &v), "non-digit minor rejected");
	ASSERT(!version_parse("1.", &v), "missing minor rejected");
	ASSERT(!version_parse("1.2.x", &v), "non-digit patch rejected");
	ASSERT(!version_parse("1.2.", &v), "missing patch rejected");
	PASS();
}

TEST(covp_version_satisfies_edges)
{
	ASSERT(version_satisfies(nullptr, ">= 1.0"), "null actual satisfies");
	ASSERT(version_satisfies("1.0", nullptr), "null constraint satisfies");
	ASSERT(version_satisfies("*", ">= 5.0"), "star actual satisfies anything");
	ASSERT(version_satisfies("not-a-version", ">= 1.0"), "unparseable actual passes");
	ASSERT(version_satisfies("1.2.3", ">= 1.0, *"), "star segment passes");
	ASSERT(version_satisfies("1.2.3", " "), "blank constraint passes");
	ASSERT(version_satisfies("1.2.3", ">="), "operator without version passes");
	ASSERT(version_satisfies("1.2.3", ">= zzz"), "unparseable constraint version passes");
	ASSERT(version_satisfies("1.2.3", "1.2.3"), "bare version means equality");
	ASSERT(!version_satisfies("1.2.4", "1.2.3"), "bare version mismatch fails");
	ASSERT(!version_satisfies("1.2.3", ">= 1.0, < 1.2"), "second segment fails");
	ASSERT(version_satisfies("1.2.3", "!= 1.2.4, <= 1.2.3, > 1.0"), "ne/le/gt satisfied");
	ASSERT(!version_satisfies("1.2.3", "!= 1.2.3"), "ne violated");
	PASS();
}

/* ------------------------------------------------------------------ */
/* lockfile.c                                                         */
/* ------------------------------------------------------------------ */

TEST(covp_lockfile_parse_invalid_toml)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds path = write_file(dir, "Coffee.lock", "this is = = not toml [[[\n");
	ASSERT(path != nullptr, "write file");

	lockfile_t *lf = lockfile_parse(path);
	ASSERT(lf == nullptr, "invalid TOML must be rejected");

	sdsfree(path);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_lockfile_parse_mixed_dependencies)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds path = write_file(dir, "Coffee.lock",
	                      "version = 1\n"
	                      "dependencies = [ \"junk\", { name = \"real\", version = \"2.0\", commit = \"abc123\" } ]\n");
	ASSERT(path != nullptr, "write file");

	lockfile_t *lf = lockfile_parse(path);
	ASSERT(lf != nullptr, "mixed array must parse");
	ASSERT(lf->deps_count == 1, "non-table element skipped without a hole");
	ASSERT(strcmp(lf->deps[0].name, "real") == 0, "table element kept");
	ASSERT(strcmp(lf->deps[0].commit, "abc123") == 0, "commit parsed");

	lockfile_free(lf);
	sdsfree(path);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_lockfile_write_roundtrip)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds path = sub_path(dir, "Coffee.lock");

	ASSERT(lockfile_write(nullptr, nullptr) == -1, "null args rejected");

	lockfile_t lf = { 0 };
	lf.version    = 1;
	ASSERT(lockfile_write(path, nullptr) == -1, "null lockfile rejected");
	ASSERT(lockfile_write(nullptr, &lf) == -1, "null path rejected");

	lf.package_name    = sdsnew("proj");
	lf.package_version = sdsnew("0.3.0");
	lf.deps            = safe_calloc(2, sizeof(lockfile_dep_t));
	lf.deps_count      = 2;
	lf.deps[0].name    = sdsnew("alpha");
	lf.deps[0].version = sdsnew("1.0.0");
	lf.deps[0].path    = sdsnew("deps/alpha");
	lf.deps[0].commit  = sdsnew("deadbeef");
	/* second entry has no fields at all: nothing but the header is written */

	ASSERT(lockfile_write(path, &lf) == 0, "write succeeds");

	lockfile_t *back = lockfile_parse(path);
	ASSERT(back != nullptr, "re-parse");
	ASSERT(strcmp(back->package_name, "proj") == 0, "package name");
	ASSERT(strcmp(back->package_version, "0.3.0") == 0, "package version");
	ASSERT(back->deps_count == 2, "two deps");
	ASSERT(strcmp(back->deps[0].commit, "deadbeef") == 0, "commit survives");
	ASSERT(back->deps[1].name == nullptr, "empty entry stays empty");

	lockfile_dep_t *found = lockfile_find_dep(back, "alpha");
	ASSERT(found != nullptr && strcmp(found->path, "deps/alpha") == 0, "find alpha");
	ASSERT(lockfile_find_dep(back, "missing") == nullptr, "missing dep");
	ASSERT(lockfile_find_dep(nullptr, "alpha") == nullptr, "null lockfile");
	ASSERT(lockfile_find_dep(back, nullptr) == nullptr, "null name");

	lockfile_free(back);
	sdsfree(lf.package_name);
	sdsfree(lf.package_version);
	sdsfree(lf.deps[0].name);
	sdsfree(lf.deps[0].version);
	sdsfree(lf.deps[0].path);
	sdsfree(lf.deps[0].commit);
	safe_free(lf.deps);
	sdsfree(path);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_lockfile_write_rename_failure)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	/* The target is a non-empty directory, so rename(2) over it fails. */
	sds target = sub_path(dir, "Coffee.lock");
	sds inner  = sdscatprintf(sdsempty(), "%s/keep", target);
	ASSERT(mkdir(target, 0755) == 0, "mkdir target");
	ASSERT(mkdir(inner, 0755) == 0, "mkdir inner");

	lockfile_t lf = { 0 };
	lf.version    = 1;
	ASSERT(lockfile_write(target, &lf) == -1, "rename failure reported");

	sds tmp = tmp_path_for(target);
	ASSERT(access(tmp, F_OK) != 0, "staging file removed after failure");

	sdsfree(tmp);
	sdsfree(inner);
	sdsfree(target);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_lockfile_write_flush_failure)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds path = sub_path(dir, "Coffee.lock");

	lockfile_t lf   = { 0 };
	lf.version      = 1;
	lf.package_name = sdsnew("proj");

	/* Cap the file size so the buffered data cannot be flushed (EFBIG). */
	struct rlimit old_limit;
	ASSERT(getrlimit(RLIMIT_FSIZE, &old_limit) == 0, "getrlimit");
	struct rlimit tiny       = old_limit;
	tiny.rlim_cur            = 8;
	void (*old_handler)(int) = signal(SIGXFSZ, SIG_IGN);
	fflush(stdout);
	ASSERT(setrlimit(RLIMIT_FSIZE, &tiny) == 0, "setrlimit");
	i64 ret = lockfile_write(path, &lf);
	setrlimit(RLIMIT_FSIZE, &old_limit);
	signal(SIGXFSZ, old_handler);

	ASSERT(ret == -1, "flush failure reported");
	ASSERT(access(path, F_OK) != 0, "target not created");

	sdsfree(lf.package_name);
	sdsfree(path);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* manifest.c                                                         */
/* ------------------------------------------------------------------ */

TEST(covp_manifest_parse_invalid_toml)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[package\nname = = \n");
	ASSERT(m == nullptr, "invalid TOML must be rejected");

	sds missing = sub_path(dir, "nope.toml");
	ASSERT(manifest_parse(missing) == nullptr, "missing file rejected");

	sdsfree(missing);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_manifest_parse_feature_edge_cases)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"p\"\n\n"
	                                         "[features]\n"
	                                         "scalar = \"not an array\"\n"
	                                         "\"bad name\" = [\"x\"]\n"
	                                         "\"\" = []\n"
	                                         "ok = [\"p\"]\n");
	ASSERT(m != nullptr, "parse");
	ASSERT(m->features_count == 3, "scalar entry is not a feature");
	bool saw_ok = false;
	for (size_t i = 0; i < m->features_count; i++) {
		if (strcmp(m->features[i].name, "ok") == 0) {
			saw_ok = true;
			ASSERT(m->features[i].deps_count == 1, "ok has one dep");
		}
	}
	ASSERT(saw_ok, "valid feature kept");

	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_manifest_parse_dependency_arrays)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	/* [[dependencies]] entries: no name, invalid name, valid name */
	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"p\"\n\n"
	                                         "[[dependencies]]\npath = \"nowhere\"\n\n"
	                                         "[[dependencies]]\nname = \"../evil\"\n\n"
	                                         "[[dependencies]]\nname = \"good\"\nversion = \"1.2\"\noptional = true\n");
	ASSERT(m != nullptr, "parse array of tables");
	ASSERT(m->dependencies.deps_count == 1, "only the valid entry survives");
	ASSERT(strcmp(m->dependencies.deps[0].name, "good") == 0, "valid name kept");
	ASSERT(strcmp(m->dependencies.deps[0].version, "1.2") == 0, "version kept");
	manifest_free(m);

	/* mixed array: string elements are flat deps, table elements structured */
	m = parse_manifest_text(dir, "dependencies = [ \"flat = \\\"1.0\\\"\", { name = \"tbl\", path = \"deps/tbl\" } ]\n"
	                             "[package]\nname = \"p\"\n");
	ASSERT(m != nullptr, "parse mixed array");
	ASSERT(m->package.dependencies_count == 1, "one flat dep");
	ASSERT(m->dependencies.deps_count == 1, "one structured dep");
	ASSERT(strcmp(m->dependencies.deps[0].name, "tbl") == 0, "structured name");
	manifest_free(m);

	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_manifest_parse_test_and_compilation)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"p\"\n\n"
	                                         "[test]\nsources = [\"t/a.c\", \"t/b.c\"]\n"
	                                         "harness = \"t/main.c\"\nframework = \"unity\"\n\n"
	                                         "[compilation]\nMakefile = \"legacy\"\n");
	ASSERT(m != nullptr, "parse");
	ASSERT(m->test.sources_count == 2, "two test sources");
	ASSERT(strcmp(m->test.sources[1], "t/b.c") == 0, "second test source");
	ASSERT(strcmp(m->test.harness, "t/main.c") == 0, "harness");
	ASSERT(strcmp(m->test.framework, "unity") == 0, "framework");
	ASSERT(strcmp(m->compilation.makefile, "legacy") == 0, "compilation makefile");

	/* Write and re-parse: the [test] and [compilation] sections round-trip */
	sds out = sub_path(dir, "Out.toml");
	ASSERT(manifest_write(out, m) == 0, "write");
	sds text = read_all(out);
	ASSERT(text != nullptr, "read back");
	ASSERT(strstr(text, "[test]") != nullptr, "test section written");
	ASSERT(strstr(text, "harness = \"t/main.c\"") != nullptr, "harness written");
	ASSERT(strstr(text, "framework = \"unity\"") != nullptr, "framework written");
	ASSERT(strstr(text, "Makefile = \"legacy\"") != nullptr, "compilation written");
	manifest_t *again = manifest_parse(out);
	ASSERT(again != nullptr, "re-parse");
	ASSERT(again->test.sources_count == 2, "sources round-trip");
	ASSERT(strcmp(again->compilation.makefile, "legacy") == 0, "makefile round-trip");

	manifest_free(again);
	sdsfree(text);
	sdsfree(out);
	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_manifest_write_all_fields)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds path = sub_path(dir, "Coffee.toml");

	manifest_t *m          = safe_calloc(1, sizeof(manifest_t));
	m->package.name        = sdsnew("full");
	m->package.version     = sdsnew("1.0.0");
	m->package.edition     = sdsnew("c23");
	m->package.description = sdsnew("desc");
	m->package.license     = sdsnew("MIT");
	m->package.repository  = sdsnew("https://example.invalid/full");
	m->package.authors     = sdsnew("someone");

	m->dependencies.deps_count = 2;
	m->dependencies.deps       = safe_calloc(2, sizeof(dependency_t));
	/* deps[0] has no name and is skipped by the writer */
	m->dependencies.deps[1].name     = sdsnew("rich");
	m->dependencies.deps[1].git      = sdsnew("https://example.invalid/rich.git");
	m->dependencies.deps[1].path     = sdsnew("deps/rich");
	m->dependencies.deps[1].version  = sdsnew("2.0");
	m->dependencies.deps[1].branch   = sdsnew("main");
	m->dependencies.deps[1].tag      = sdsnew("v2");
	m->dependencies.deps[1].rev      = sdsnew("abc");
	m->dependencies.deps[1].optional = true;

	m->package.dependencies_count = 5;
	m->package.dependencies       = safe_calloc(5, sizeof(sds));
	/* [0] null: skipped; [1] bare key without structured twin: gets "*" */
	m->package.dependencies[1] = sdsnew("barekey");
	m->package.dependencies[2] = sdsnew("flat = \"1.5\"");
	m->package.dependencies[3] = sdsnew("rich"); /* structured twin: skipped */
	m->package.dependencies[4] = sdsnew("../evil");

	m->features_count         = 1;
	m->features               = safe_calloc(1, sizeof(feature_def_t));
	m->features[0].name       = sdsnew("multi");
	m->features[0].deps_count = 2;
	m->features[0].deps       = safe_calloc(2, sizeof(sds));
	m->features[0].deps[0]    = sdsnew("one");
	m->features[0].deps[1]    = sdsnew("two");

	ASSERT(manifest_write(path, m) == 0, "write");
	sds text = read_all(path);
	ASSERT(text != nullptr, "read back");
	ASSERT(strstr(text, "license = \"MIT\"") != nullptr, "license");
	ASSERT(strstr(text, "repository = ") != nullptr, "repository");
	ASSERT(strstr(text, "authors = \"someone\"") != nullptr, "authors");
	ASSERT(strstr(text, "barekey = \"*\"") != nullptr, "bare key gets wildcard");
	ASSERT(strstr(text, "flat = \"1.5\"") != nullptr, "flat dep verbatim");
	ASSERT(strstr(text, "evil") == nullptr, "invalid dep dropped");
	ASSERT(strstr(text, "multi = [\"one\", \"two\"]") != nullptr, "feature list joined with comma");

	manifest_t *back = manifest_parse(path);
	ASSERT(back != nullptr, "written file re-parses");
	ASSERT(back->dependencies.deps_count == 1, "one structured dep");
	dependency_t *d = &back->dependencies.deps[0];
	ASSERT(strcmp(d->name, "rich") == 0, "name");
	ASSERT(strcmp(d->git, "https://example.invalid/rich.git") == 0, "git");
	ASSERT(strcmp(d->path, "deps/rich") == 0, "path");
	ASSERT(strcmp(d->version, "2.0") == 0, "version");
	ASSERT(strcmp(d->branch, "main") == 0, "branch");
	ASSERT(strcmp(d->tag, "v2") == 0, "tag");
	ASSERT(strcmp(d->rev, "abc") == 0, "rev");
	ASSERT(d->optional, "optional");

	manifest_free(back);
	sdsfree(text);
	manifest_free(m);
	sdsfree(path);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_manifest_write_failures)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t m   = { 0 };
	m.package.name = sdsnew("w");
	ASSERT(manifest_write(nullptr, &m) == -1, "null path");
	ASSERT(manifest_write(dir, nullptr) == -1, "null manifest");

	/* A planted symlink at the staging path makes the nofollow open fail. */
	sds path    = sub_path(dir, "Coffee.toml");
	sds staging = tmp_path_for(path);
	ASSERT(symlink("/nonexistent-target", staging) == 0, "plant symlink");
	ASSERT(manifest_write(path, &m) == -1, "open failure reported");
	ASSERT(unlink(staging) == 0, "remove symlink");
	sdsfree(staging);

	/* Rename over a non-empty directory fails */
	sds target = sub_path(dir, "Target.toml");
	sds inner  = sdscatprintf(sdsempty(), "%s/keep", target);
	ASSERT(mkdir(target, 0755) == 0, "mkdir target");
	ASSERT(mkdir(inner, 0755) == 0, "mkdir inner");
	ASSERT(manifest_write(target, &m) == -1, "rename failure reported");
	sds tmp = tmp_path_for(target);
	ASSERT(access(tmp, F_OK) != 0, "staging file removed");
	sdsfree(tmp);

	/* Flush failure: cap the file size */
	struct rlimit old_limit;
	ASSERT(getrlimit(RLIMIT_FSIZE, &old_limit) == 0, "getrlimit");
	struct rlimit tiny       = old_limit;
	tiny.rlim_cur            = 4;
	void (*old_handler)(int) = signal(SIGXFSZ, SIG_IGN);
	fflush(stdout);
	ASSERT(setrlimit(RLIMIT_FSIZE, &tiny) == 0, "setrlimit");
	i64 ret = manifest_write(path, &m);
	setrlimit(RLIMIT_FSIZE, &old_limit);
	signal(SIGXFSZ, old_handler);
	ASSERT(ret == -1, "flush failure reported");

	sdsfree(inner);
	sdsfree(target);
	sdsfree(path);
	sdsfree(m.package.name);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_manifest_free_and_remove_edges)
{
	manifest_free(nullptr);

	ASSERT(!manifest_remove_dependency(nullptr, "x"), "null manifest");
	manifest_t *m = safe_calloc(1, sizeof(manifest_t));
	ASSERT(!manifest_remove_dependency(m, nullptr), "null name");

	m->package.dependencies_count = 3;
	m->package.dependencies       = safe_calloc(3, sizeof(sds));
	m->package.dependencies[1]    = sdsnew("keep = \"1\"");
	m->package.dependencies[2]    = sdsnew("gone = \"2\"");
	ASSERT(manifest_remove_dependency(m, "gone"), "flat dep removed past a null slot");
	ASSERT(m->package.dependencies_count == 2, "count decremented");
	ASSERT(!manifest_remove_dependency(m, "absent"), "absent dep");

	m->test.sources_count = 1;
	m->test.sources       = safe_calloc(1, sizeof(sds));
	m->test.sources[0]    = sdsnew("t.c");
	m->test.harness       = sdsnew("h.c");
	m->test.framework     = sdsnew("f");
	manifest_free(m);
	PASS();
}

TEST(covp_manifest_extract_dep_info_trailing_space)
{
	sds name    = nullptr;
	sds version = nullptr;
	manifest_extract_dep_info("lonely \t", &name, &version);
	ASSERT(name != nullptr && strcmp(name, "lonely") == 0, "trailing blanks trimmed");
	ASSERT(version != nullptr && strcmp(version, "*") == 0, "wildcard version");
	sdsfree(name);
	sdsfree(version);
	PASS();
}

/* ------------------------------------------------------------------ */
/* coffee_features.c                                                  */
/* ------------------------------------------------------------------ */

static bool package_has_feature(resolved_features_t *rf, const char *package, const char *feature)
{
	sds  p = sdsnew(package);
	sds  f = sdsnew(feature);
	bool r = features_is_enabled(rf, p, f);
	sdsfree(p);
	sdsfree(f);
	return r;
}

TEST(covp_manifest_parse_non_string_elements)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "bin = [ \"stray\", { name = \"tool\", src = [\"t.c\"] } ]\n"
	                                         "[package]\nname = \"p\"\n\n"
	                                         "[features]\nf = [1, \"libq/z\"]\n");
	ASSERT(m != nullptr, "parse");
	ASSERT(m->bin_count == 2, "both bin elements counted");
	ASSERT(m->bin[0].name == nullptr, "string element leaves an empty slot");
	ASSERT(strcmp(m->bin[1].name, "tool") == 0, "table element parsed");
	ASSERT(m->features_count == 1 && m->features[0].deps_count == 2, "feature has two slots");
	ASSERT(m->features[0].deps[0] == nullptr, "non-string feature dep is null");

	/* Resolution tolerates the null dep and still follows the valid one */
	sds                  req[] = { sdsnew("f") };
	resolved_features_t *rf    = features_resolve(m, req, 1, false, true);
	ASSERT(rf != nullptr, "resolve");
	ASSERT(package_has_feature(rf, "libq", "z"), "valid cross ref followed");

	features_free(rf);
	sdsfree(req[0]);
	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_features_unnamed_root)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[features]\nf = [\"dep/x\"]\n");
	ASSERT(m != nullptr, "parse");
	ASSERT(m->package.name == nullptr, "no package name");

	sds                  req[] = { sdsnew("f") };
	resolved_features_t *rf    = features_resolve(m, req, 1, false, true);
	ASSERT(rf != nullptr, "resolve");
	ASSERT(rf->package_count == 1, "cross refs not followed without a package name");
	ASSERT(package_has_feature(rf, "root", "f"), "feature enabled on fallback root name");

	features_free(rf);
	sdsfree(req[0]);
	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_features_shared_foreign_package)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"app\"\n\n"
	                                         "[features]\nf = [\"libx/a\", \"libx/b\"]\n");
	ASSERT(m != nullptr, "parse");

	sds                  req[] = { sdsnew("f") };
	resolved_features_t *rf    = features_resolve(m, req, 1, false, false);
	ASSERT(rf != nullptr, "resolve");
	ASSERT(rf->package_count == 2, "app plus libx");
	ASSERT(package_has_feature(rf, "libx", "a"), "libx/a");
	ASSERT(package_has_feature(rf, "libx", "b"), "libx/b");

	features_free(rf);
	sdsfree(req[0]);
	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_features_default_skipped_in_flags)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"app\"\n\n[features]\nfast-mode = []\n");
	ASSERT(m != nullptr, "parse");

	sds                  req[] = { sdsnew("default"), sdsnew("fast-mode") };
	resolved_features_t *rf    = features_resolve(m, req, 2, false, true);
	ASSERT(rf != nullptr, "resolve");

	sds    app   = sdsnew("app");
	size_t count = 0;
	sds   *flags = features_to_compiler_flags(rf, app, &count);
	ASSERT(flags != nullptr && count == 1, "default produces no define");
	ASSERT(strstr(flags[0], "FAST_MODE") != nullptr, "dash becomes underscore");

	for (size_t i = 0; i < count; i++) {
		sdsfree(flags[i]);
	}
	safe_free(flags);
	sdsfree(app);
	features_free(rf);
	sdsfree(req[0]);
	sdsfree(req[1]);
	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_features_transitive_resolution)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	sds libx = sdscatprintf(sdsempty(), "%s/deps/libx", dir);
	sds liby = sdscatprintf(sdsempty(), "%s/deps/liby", dir);
	sds libz = sdscatprintf(sdsempty(), "%s/deps/libz", dir);
	sds libr = sdscatprintf(sdsempty(), "%s/deps/libren", dir);
	sds deps = sdscatprintf(sdsempty(), "%s/deps", dir);
	ASSERT(mkdir(deps, 0755) == 0, "mkdir deps");
	ASSERT(mkdir(libx, 0755) == 0 && mkdir(liby, 0755) == 0, "mkdir libx liby");
	ASSERT(mkdir(libz, 0755) == 0 && mkdir(libr, 0755) == 0, "mkdir libz libren");

	sds p1 = write_file(libx, "Coffee.toml", "[package]\nname = \"libx\"\n\n[features]\na = [\"liby/c\"]\n");
	sds p2 = write_file(liby, "library.toml", "[package]\nname = \"liby\"\n");
	sds p3 = write_file(libr, "Coffee.toml", "[package]\nname = \"renamed\"\n");
	ASSERT(p1 != nullptr && p2 != nullptr && p3 != nullptr, "write dep manifests");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"app\"\n\n"
	                                         "[features]\nf = [\"libx/a\", \"libz/q\", \"libren/w\", \"nodir/n\"]\n");
	ASSERT(m != nullptr, "parse root");

	char *saved_cwd = getcwd(nullptr, 0);
	ASSERT(saved_cwd != nullptr, "getcwd");
	ASSERT(chdir(dir) == 0, "chdir");

	sds                  req[] = { sdsnew("f") };
	resolved_features_t *rf    = features_resolve(m, req, 1, false, false);

	ASSERT(chdir(saved_cwd) == 0, "restore cwd");
	free(saved_cwd);

	ASSERT(rf != nullptr, "resolve");
	ASSERT(package_has_feature(rf, "libx", "a"), "libx/a enabled");
	ASSERT(package_has_feature(rf, "liby", "c"), "liby/c discovered through libx");
	ASSERT(package_has_feature(rf, "renamed", "w") == false, "canonical name gets an empty set");
	bool saw_renamed = false;
	for (size_t i = 0; i < rf->package_count; i++) {
		if (strcmp(rf->package_names[i], "renamed") == 0) {
			saw_renamed = true;
		}
	}
	ASSERT(saw_renamed, "manifest package name registered");

	features_free(rf);
	sdsfree(req[0]);
	manifest_free(m);
	sdsfree(p1);
	sdsfree(p2);
	sdsfree(p3);
	sdsfree(libx);
	sdsfree(liby);
	sdsfree(libz);
	sdsfree(libr);
	sdsfree(deps);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* makefile.c                                                         */
/* ------------------------------------------------------------------ */

TEST(covp_makefile_render_unsafe_values)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"mk\"\nedition = \"\"\n"
	                                         "sources = [\"\", \"src/a.c\", \"src/a.c\", \"src/$x.c\"]\n");
	ASSERT(m != nullptr, "parse");

	sds out = makefile_render(m, nullptr, nullptr, "", false, false, false);
	ASSERT(out != nullptr, "render");
	ASSERT(strstr(out, "-std=c23") != nullptr, "empty edition falls back");
	ASSERT(strstr(out, "TARGET_DIR := build\n") != nullptr, "empty target dir falls back");
	ASSERT(strstr(out, "SOURCES := src/a.c\n") != nullptr, "empty and duplicate sources collapse, unsafe skipped");
	sdsfree(out);

	ASSERT(strcmp(makefile_resolve_target_dir(""), "build") == 0, "resolve empty target dir");
	ASSERT(strcmp(makefile_resolve_target_dir(nullptr), "build") == 0, "resolve null target dir");

	sdsfree(m->package.name);
	m->package.name = sdsnew("");
	ASSERT(makefile_render(m, nullptr, nullptr, nullptr, false, false, false) == nullptr,
	       "empty package name rejected");
	ASSERT(makefile_render(nullptr, nullptr, nullptr, nullptr, false, false, false) == nullptr, "null manifest");

	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_makefile_render_bin_sources)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"mk\"\n\n"
	                                         "[[bin]]\nname = \"tool\"\nsrc = [\"tool/*.c\", \"common/util.c\"]\n");
	ASSERT(m != nullptr, "parse");

	sds out = makefile_render(m, nullptr, nullptr, nullptr, true, false, false);
	ASSERT(out != nullptr, "render");
	ASSERT(strstr(out, "$(wildcard tool/*.c)") != nullptr, "bin glob used");
	ASSERT(strstr(out, "common/util.c") != nullptr, "bin file used");
	ASSERT(strstr(out, "SOURCES := common/util.c $(wildcard tool/*.c)\n") != nullptr, "default glob not used");
	sdsfree(out);

	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_makefile_generate_errors)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	options opt = { 0 };

	ASSERT(makefile_generate(nullptr, dir, nullptr, &opt, false) == 1, "null manifest");

	/* Root name that dep_graph rejects: no graph */
	manifest_t *bad = parse_manifest_text(dir, "[package]\nname = \"a b\"\n");
	ASSERT(bad != nullptr, "parse bad");
	ASSERT(makefile_generate(bad, dir, nullptr, &opt, false) == 1, "graph failure");
	ASSERT(makefile_generate(bad, nullptr, nullptr, &opt, false) == 1, "null project dir");
	manifest_free(bad);

	/* No package name: the graph uses "root" but the renderer refuses it */
	manifest_t *noname = parse_manifest_text(dir, "[features]\nf = []\n");
	ASSERT(noname != nullptr, "parse noname");
	ASSERT(makefile_generate(noname, dir, nullptr, &opt, false) == 1, "render failure");
	manifest_free(noname);

	sds make_path = sub_path(dir, "Makefile");
	ASSERT(access(make_path, F_OK) != 0, "no Makefile written on error");
	sdsfree(make_path);

	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_makefile_generate_with_lockfile)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	options opt = { 0 };

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"locked\"\nversion = \"1.0.0\"\n");
	ASSERT(m != nullptr, "parse");

	sds lock_path = sub_path(dir, "Coffee.lock");
	sds written = write_file(dir, "Coffee.lock", "version = 1\n\n[package]\nname = \"locked\"\nversion = \"1.0.0\"\n");
	ASSERT(written != nullptr, "write lockfile");
	sdsfree(written);

	ASSERT(makefile_generate(m, dir, nullptr, &opt, false) == 0, "generate with lockfile");
	sds make_path = sub_path(dir, "Makefile");
	sds text      = read_all(make_path);
	ASSERT(text != nullptr && strstr(text, "TARGET := $(TARGET_DIR)/locked") != nullptr, "Makefile content");

	sdsfree(text);
	sdsfree(make_path);
	sdsfree(lock_path);
	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_makefile_generate_write_failures)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	options opt = { 0 };

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"wf\"\nversion = \"1.0.0\"\n");
	ASSERT(m != nullptr, "parse");

	/* Makefile is a non-empty directory: rename over it fails */
	sds make_path = sub_path(dir, "Makefile");
	sds inner     = sdscatprintf(sdsempty(), "%s/keep", make_path);
	ASSERT(mkdir(make_path, 0755) == 0, "mkdir Makefile");
	ASSERT(mkdir(inner, 0755) == 0, "mkdir inner");
	ASSERT(makefile_generate(m, dir, nullptr, &opt, false) == 1, "rename failure reported");
	ASSERT(!dir_contains_tmp_files(dir), "staging file removed");
	ASSERT(rmdir(inner) == 0 && rmdir(make_path) == 0, "cleanup dirs");

	/* Flush failure: cap the file size */
	struct rlimit old_limit;
	ASSERT(getrlimit(RLIMIT_FSIZE, &old_limit) == 0, "getrlimit");
	struct rlimit tiny       = old_limit;
	tiny.rlim_cur            = 8;
	void (*old_handler)(int) = signal(SIGXFSZ, SIG_IGN);
	fflush(stdout);
	ASSERT(setrlimit(RLIMIT_FSIZE, &tiny) == 0, "setrlimit");
	i64 ret = makefile_generate(m, dir, nullptr, &opt, false);
	setrlimit(RLIMIT_FSIZE, &old_limit);
	signal(SIGXFSZ, old_handler);
	ASSERT(ret == 1, "flush failure reported");
	ASSERT(access(make_path, F_OK) != 0, "no Makefile after failed flush");
	ASSERT(!dir_contains_tmp_files(dir), "staging file removed after flush failure");

	sdsfree(inner);
	sdsfree(make_path);
	manifest_free(m);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* Staging-file fsync failures                                        */
/* ------------------------------------------------------------------ */

/* fsync(2) fails with EINVAL on a pipe.  Plant a FIFO where a writer will
 * create its staging file and keep a non-blocking reader open so the
 * writer's O_WRONLY open succeeds.  Returns the reader fd, or -1. */
static int plant_fifo_with_reader(const char *staging)
{
	if (mkfifo(staging, 0600) != 0) {
		return -1;
	}
	return safe_open(staging, O_RDONLY | O_NONBLOCK);
}

TEST(covp_staging_fsync_failure)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	/* lockfile_write */
	lockfile_t lf = { 0 };
	lf.version    = 1;
	sds lock_path = sub_path(dir, "Coffee.lock");
	sds staging   = tmp_path_for(lock_path);
	int reader    = plant_fifo_with_reader(staging);
	ASSERT(reader >= 0, "plant fifo");
	ASSERT(lockfile_write(lock_path, &lf) == -1, "lockfile fsync failure reported");
	ASSERT(access(staging, F_OK) != 0, "lockfile staging removed");
	ASSERT(access(lock_path, F_OK) != 0, "no lockfile written");
	safe_close(reader);
	sdsfree(staging);
	sdsfree(lock_path);

	/* manifest_write */
	manifest_t m   = { 0 };
	m.package.name = sdsnew("w");
	sds toml_path  = sub_path(dir, "Out.toml");
	staging        = tmp_path_for(toml_path);
	reader         = plant_fifo_with_reader(staging);
	ASSERT(reader >= 0, "plant fifo");
	ASSERT(manifest_write(toml_path, &m) == -1, "manifest fsync failure reported");
	ASSERT(access(staging, F_OK) != 0, "manifest staging removed");
	safe_close(reader);
	sdsfree(staging);
	sdsfree(toml_path);
	sdsfree(m.package.name);

	/* makefile_generate and the dependency graph cache */
	manifest_t *pm = parse_manifest_text(dir, "[package]\nname = \"fs\"\nversion = \"1.0.0\"\n");
	ASSERT(pm != nullptr, "parse");
	options opt     = { 0 };
	sds     mk_path = sub_path(dir, "Makefile");
	staging         = tmp_path_for(mk_path);
	reader          = plant_fifo_with_reader(staging);
	ASSERT(reader >= 0, "plant fifo");
	ASSERT(makefile_generate(pm, dir, nullptr, &opt, false) == 1, "makefile fsync failure reported");
	ASSERT(access(staging, F_OK) != 0, "makefile staging removed");
	safe_close(reader);
	sdsfree(staging);
	sdsfree(mk_path);

	sds cache_dir = sub_path(dir, ".coffee");
	ASSERT(mkdir(cache_dir, 0755) == 0, "mkdir .coffee");
	sdsfree(cache_dir);
	cache_dir = sub_path(dir, ".coffee/build-cache");
	ASSERT(mkdir(cache_dir, 0755) == 0, "mkdir build-cache");
	sds cache_path = sdscatprintf(sdsempty(), "%s/fs.graph", cache_dir);
	staging        = tmp_path_for(cache_path);
	reader         = plant_fifo_with_reader(staging);
	ASSERT(reader >= 0, "plant fifo");
	dep_graph_t *g = dep_graph_get(pm, nullptr, false, dir);
	ASSERT(g != nullptr, "graph is still returned when the cache cannot be written");
	ASSERT(access(staging, F_OK) != 0, "cache staging removed");
	ASSERT(access(cache_path, F_OK) != 0, "no cache written");
	safe_close(reader);
	dep_graph_free(g);

	sdsfree(staging);
	sdsfree(cache_path);
	sdsfree(cache_dir);
	manifest_free(pm);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* dep_graph.c                                                        */
/* ------------------------------------------------------------------ */

TEST(covp_dep_graph_transitive_self_dependency)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	sds deps = sub_path(dir, "deps");
	sds libs = sub_path(dir, "deps/libs");
	ASSERT(mkdir(deps, 0755) == 0 && mkdir(libs, 0755) == 0, "mkdir deps");
	sds p1 = write_file(libs, "Coffee.toml",
	                    "[package]\nname = \"libs\"\nversion = \"1.0.0\"\n\n"
	                    "[[dependencies]]\nname = \"libs\"\nversion = \"1.0.0\"\n");
	ASSERT(p1 != nullptr, "write dep manifest");
	manifest_t *m = parse_manifest_text(dir, "dependencies = [\"libs\"]\n\n[package]\nname = \"app\"\n");
	ASSERT(m != nullptr, "parse root");

	char *saved_cwd = getcwd(nullptr, 0);
	ASSERT(saved_cwd != nullptr, "getcwd");
	ASSERT(chdir(dir) == 0, "chdir");
	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(chdir(saved_cwd) == 0, "restore cwd");
	free(saved_cwd);

	ASSERT(g != nullptr, "graph created");
	ASSERT(dep_graph_count(g) == 2, "a structured self-dependency adds no node");

	dep_graph_free(g);
	manifest_free(m);
	sdsfree(p1);
	sdsfree(libs);
	sdsfree(deps);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* cmdline.c                                                          */
/* ------------------------------------------------------------------ */

/* Run cmdline_parser(argv) in a child with stdout captured.  The parser
 * exits for --help/--version, which must not take the test runner down.
 * Returns the child's exit status, or -1 on harness failure. */
static i64 run_parser_in_child(char *argv[], int argc, sds *captured)
{
	int fds[2];
	if (safe_pipe(fds) != 0) {
		return -1;
	}
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		/* The runner's atexit hook deletes COFFEE_HOME; hide it. */
		unsetenv("COFFEE_HOME");
		safe_close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		safe_close(fds[1]);
		struct cli_args args;
		cmdline_parser(argc, argv, &args);
		exit(EXIT_FAILURE); /* only reached if the parser did not exit */
	}
	safe_close(fds[1]);
	*captured = sdsempty();
	char    buf[1'024];
	ssize_t n = read(fds[0], buf, sizeof(buf));
	while (n > 0) {
		*captured = sdscatlen(*captured, buf, (size_t)n);
		n         = read(fds[0], buf, sizeof(buf));
	}
	safe_close(fds[0]);
	int status = 0;
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
		return -1;
	}
	return WEXITSTATUS(status);
}

TEST(covp_cmdline_help_exits_zero)
{
	char *argv[] = { (char *)"coffee", (char *)"-h", nullptr };
	sds   out    = nullptr;
	i64   status = run_parser_in_child(argv, 2, &out);
	ASSERT(status == 0, "help exits with success");
	ASSERT(strstr(out, "Usage") != nullptr || strstr(out, "usage") != nullptr, "help text printed");
	sdsfree(out);
	PASS();
}

TEST(covp_cmdline_version_exits_zero)
{
	char *argv[] = { (char *)"coffee", (char *)"--version", nullptr };
	sds   out    = nullptr;
	i64   status = run_parser_in_child(argv, 2, &out);
	ASSERT(status == 0, "version exits with success");
	ASSERT(strncmp(out, "coffee ", 7) == 0, "version banner printed");
	sdsfree(out);
	PASS();
}

TEST(covp_cmdline_null_args_rejected)
{
	char *argv[] = { (char *)"coffee", nullptr };
	ASSERT(cmdline_parser(1, argv, nullptr) == 1, "null args struct rejected");
	PASS();
}

TEST(covp_cmdline_jobs_out_of_range)
{
	char *negative[] = { (char *)"coffee", (char *)"-j", (char *)"-1", nullptr };
	char *too_big[]  = { (char *)"coffee", (char *)"--jobs", (char *)"3000000000", nullptr };
	char *in_range[] = { (char *)"coffee", (char *)"-j", (char *)"2147483647", nullptr };

	struct cli_args args;
	ASSERT(cmdline_parser(3, negative, &args) == 1, "negative job count rejected");
	ASSERT(cmdline_parser(3, too_big, &args) == 1, "job count above INT_MAX rejected");
	ASSERT(cmdline_parser(3, in_range, &args) == 0, "INT_MAX job count accepted");
	ASSERT(args.jobs_arg == 2'147'483'647, "job count stored");
	cmdline_parser_free(&args);
	PASS();
}

/* ------------------------------------------------------------------ */
/* version.c: operator and cursor edges                               */
/* ------------------------------------------------------------------ */

TEST(covp_version_satisfies_more_edges)
{
	ASSERT(version_satisfies("1.0.0", "!1.0.0"), "lone '!' is not an operator; unparseable passes");
	ASSERT(version_satisfies("*1", ">= 1.0"), "star prefix is not the wildcard; unparseable passes");
	ASSERT(version_satisfies("1.2.3", ">= 1.0,"), "trailing comma ends the walk");
	ASSERT(!version_satisfies("0.9.0", ">= 1.0,"), "trailing comma still checks the first segment");
	PASS();
}

/* ------------------------------------------------------------------ */
/* lockfile.c: empty dependency array and lockfile_same_deps          */
/* ------------------------------------------------------------------ */

TEST(covp_lockfile_parse_empty_dependencies)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds path = write_file(dir, "Coffee.lock", "version = 1\ndependencies = []\n");
	ASSERT(path != nullptr, "write file");

	lockfile_t *lf = lockfile_parse(path);
	ASSERT(lf != nullptr, "empty array parses");
	ASSERT(lf->deps_count == 0 && lf->deps == nullptr, "no deps allocated");

	lockfile_free(lf);
	sdsfree(path);
	drop_scratch_dir(dir);
	PASS();
}

/* Fields of one lockfile entry, in lockfile_dep_t order; nullptr = absent. */
struct lock_fields {
	const char *name;
	const char *version;
	const char *path;
	const char *commit;
	const char *checksum;
};

static sds sds_or_null(const char *s)
{
	if (s == nullptr) {
		return nullptr;
	}
	return sdsnew(s);
}

static void lock_dep_fill(lockfile_dep_t *d, const struct lock_fields *f)
{
	d->name     = sds_or_null(f->name);
	d->version  = sds_or_null(f->version);
	d->path     = sds_or_null(f->path);
	d->commit   = sds_or_null(f->commit);
	d->checksum = sds_or_null(f->checksum);
}

static void lock_dep_clear(lockfile_dep_t *d)
{
	sdsfree(d->name);
	sdsfree(d->version);
	sdsfree(d->path);
	sdsfree(d->commit);
	sdsfree(d->checksum);
}

/* lockfile_same_deps on two single-entry lockfiles. */
static bool same_single_dep(const struct lock_fields *fa, const struct lock_fields *fb)
{
	lockfile_dep_t da = { 0 };
	lockfile_dep_t db = { 0 };
	lock_dep_fill(&da, fa);
	lock_dep_fill(&db, fb);
	lockfile_t a = { 0 };
	lockfile_t b = { 0 };
	a.deps       = &da;
	a.deps_count = 1;
	b.deps       = &db;
	b.deps_count = 1;
	bool same    = lockfile_same_deps(&a, &b);
	lock_dep_clear(&da);
	lock_dep_clear(&db);
	return same;
}

TEST(covp_lockfile_same_deps_fields)
{
	static const struct lock_fields base = { "x", "1.0", "deps/x", "abc", "sum" };
	static const struct {
		struct lock_fields other;
		bool               same;
		const char        *what;
	} cases[] = {
		{ { "x", "1.0", "deps/x", "abc", "sum" }, true, "identical entries" },
		{ { "y", "1.0", "deps/x", "abc", "sum" }, false, "name differs" },
		{ { "x", "2.0", "deps/x", "abc", "sum" }, false, "version differs" },
		{ { "x", "1.0", "deps/y", "abc", "sum" }, false, "path differs" },
		{ { "x", "1.0", "deps/x", "def", "sum" }, false, "commit differs" },
		{ { "x", "1.0", "deps/x", "abc", "other" }, false, "checksum differs" },
		{ { "x", nullptr, "deps/x", "abc", "sum" }, false, "version missing" },
		{ { "x", "1.0", nullptr, "abc", "sum" }, false, "path missing" },
		{ { "x", "1.0", "deps/x", nullptr, "sum" }, false, "commit missing" },
		{ { "x", "1.0", "deps/x", "abc", nullptr }, false, "checksum missing" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		ASSERT(same_single_dep(&base, &cases[i].other) == cases[i].same, cases[i].what);
		ASSERT(same_single_dep(&cases[i].other, &base) == cases[i].same, cases[i].what);
	}

	static const struct lock_fields bare = { "x", nullptr, nullptr, nullptr, nullptr };
	ASSERT(same_single_dep(&bare, &bare), "entries with only a name match");
	PASS();
}

TEST(covp_lockfile_same_deps_shape)
{
	static const struct lock_fields fx = { "x", "1.0", nullptr, nullptr, nullptr };
	static const struct lock_fields fy = { "y", "2.0", nullptr, nullptr, nullptr };

	lockfile_dep_t ad[2] = { 0 };
	lockfile_dep_t bd[2] = { 0 };
	lock_dep_fill(&ad[0], &fx);
	lock_dep_fill(&ad[1], &fy);
	lock_dep_fill(&bd[0], &fy);
	lock_dep_fill(&bd[1], &fx);
	lockfile_t a = { 0 };
	lockfile_t b = { 0 };
	a.deps       = ad;
	a.deps_count = 2;
	b.deps       = bd;
	b.deps_count = 2;

	ASSERT(lockfile_same_deps(&a, &b), "order does not matter");
	ASSERT(!lockfile_same_deps(nullptr, &b), "null first lockfile");
	ASSERT(!lockfile_same_deps(&a, nullptr), "null second lockfile");
	b.deps_count = 1;
	ASSERT(!lockfile_same_deps(&a, &b), "different entry counts");

	for (size_t i = 0; i < 2; i++) {
		lock_dep_clear(&ad[i]);
		lock_dep_clear(&bd[i]);
	}
	PASS();
}

/* ------------------------------------------------------------------ */
/* manifest.c: parser edges                                           */
/* ------------------------------------------------------------------ */

TEST(covp_manifest_parse_odd_sections)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");

	/* Feature names with characters just outside each allowed range */
	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"p\"\n\n"
	                                         "[features]\n\"a{b\" = []\n\"a[b\" = []\n\"a:b\" = []\n\"a!b\" = []\n"
	                                         "f = [\"g\"]\ng = [1, \"f\"]\n");
	ASSERT(m != nullptr, "parse odd feature names");
	ASSERT(m->features_count == 6, "invalid names still recorded");
	manifest_free(m);

	/* A [features] table with no array entries */
	m = parse_manifest_text(dir, "[package]\nname = \"p\"\n\n[features]\nscalar = \"x\"\n");
	ASSERT(m != nullptr, "parse scalar-only features");
	ASSERT(m->features_count == 0 && m->features == nullptr, "no features");
	manifest_free(m);

	/* An empty bin array */
	m = parse_manifest_text(dir, "bin = []\n[package]\nname = \"p\"\n");
	ASSERT(m != nullptr, "parse empty bin array");
	ASSERT(m->bin_count == 0 && m->bin == nullptr, "no bins");
	manifest_free(m);

	/* A bin with an empty src array, and a [test] table without sources */
	m = parse_manifest_text(dir, "[package]\nname = \"p\"\n\n"
	                             "[[bin]]\nname = \"t\"\nsrc = []\n\n"
	                             "[test]\nharness = \"h.c\"\n");
	ASSERT(m != nullptr, "parse empty src");
	ASSERT(m->bin_count == 1 && m->bin[0].src_count == 0, "bin without sources");
	ASSERT(m->test.sources_count == 0, "test without sources");
	ASSERT(strcmp(m->test.harness, "h.c") == 0, "harness kept");
	manifest_free(m);

	drop_scratch_dir(dir);
	PASS();
}

TEST(covp_manifest_remove_structured_past_unnamed)
{
	manifest_t *m                = safe_calloc(1, sizeof(manifest_t));
	m->dependencies.deps_count   = 2;
	m->dependencies.deps         = safe_calloc(2, sizeof(dependency_t));
	m->dependencies.deps[1].name = sdsnew("x");

	ASSERT(manifest_remove_dependency(m, "x"), "structured dep removed past an unnamed slot");
	ASSERT(m->dependencies.deps_count == 1, "count decremented");
	ASSERT(m->dependencies.deps[0].name == nullptr, "unnamed slot kept");

	manifest_free(m);
	PASS();
}

TEST(covp_manifest_extract_dep_info_edges)
{
	static const struct {
		const char *entry;
		const char *name;
		const char *version;
	} cases[] = {
		{ " \t", "", "*" },
		{ "a\t= 1.0", "a", "1.0" },
		{ " =1", " ", "1" },
		{ "a =\t\"1.0\"", "a", "1.0" },
		{ "a = 1.0", "a", "1.0" },
		{ "a = 1.0 \t", "a", "1.0" },
		{ "a = \"1.0\" ", "a", "1.0" },
		{ "a =", "a", "" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		sds name    = nullptr;
		sds version = nullptr;
		manifest_extract_dep_info(cases[i].entry, &name, &version);
		ASSERT(name != nullptr && strcmp(name, cases[i].name) == 0, cases[i].entry);
		ASSERT(version != nullptr && strcmp(version, cases[i].version) == 0, cases[i].entry);
		sdsfree(name);
		sdsfree(version);
	}

	/* Outputs start non-null so the clearing is observable */
	sds stale   = sdsnew("stale");
	sds name    = stale;
	sds version = stale;
	manifest_extract_dep_info(nullptr, &name, &version);
	ASSERT(name == nullptr && version == nullptr, "null entry clears both outputs");
	version = stale;
	manifest_extract_dep_info("a = 1", nullptr, &version);
	ASSERT(version == nullptr, "null name output clears the version output");
	name = stale;
	manifest_extract_dep_info("a = 1", &name, nullptr);
	ASSERT(name == nullptr, "null version output clears the name output");
	sdsfree(stale);
	PASS();
}

/* ------------------------------------------------------------------ */
/* manifest.c: writer edges                                           */
/* ------------------------------------------------------------------ */

static sds *sds_array2(const char *first, const char *second)
{
	sds *arr = safe_calloc(2, sizeof(sds));
	arr[0]   = sds_or_null(first);
	arr[1]   = sds_or_null(second);
	return arr;
}

/* One structured dep per optional field, so each field is written first. */
static void add_single_field_deps(manifest_t *m)
{
	m->dependencies.deps_count = 6;
	m->dependencies.deps       = safe_calloc(6, sizeof(dependency_t));
	dependency_t *d            = m->dependencies.deps;
	d[0].name                  = sdsnew("dgit");
	d[0].git                   = sdsnew("https://example.invalid/g.git");
	d[1].name                  = sdsnew("dver");
	d[1].version               = sdsnew("1.0");
	d[2].name                  = sdsnew("dbranch");
	d[2].branch                = sdsnew("main");
	d[3].name                  = sdsnew("dtag");
	d[3].tag                   = sdsnew("v1");
	d[4].name                  = sdsnew("drev");
	d[4].rev                   = sdsnew("abc");
	d[5].name                  = sdsnew("dopt");
	d[5].optional              = true;
}

/* Bins: unnamed (skipped), no sources, and sources with a null slot. */
static void add_odd_bins(manifest_t *m)
{
	m->bin_count        = 3;
	m->bin              = safe_calloc(3, sizeof(binary_target_t));
	m->bin[1].name      = sdsnew("nosrc");
	m->bin[2].name      = sdsnew("withsrc");
	m->bin[2].src_count = 2;
	m->bin[2].src       = sds_array2(nullptr, "w.c");
}

TEST(covp_manifest_write_sparse_fields)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds path = sub_path(dir, "Coffee.toml");

	manifest_t *m             = safe_calloc(1, sizeof(manifest_t));
	m->package.sources_count  = 2;
	m->package.sources        = sds_array2(nullptr, "src/a.c");
	m->package.headers_count  = 2;
	m->package.headers        = sds_array2(nullptr, "include/a.h");
	m->features_count         = 2;
	m->features               = safe_calloc(2, sizeof(feature_def_t));
	m->features[1].name       = sdsnew("f");
	m->features[1].deps_count = 2;
	m->features[1].deps       = sds_array2("x", nullptr);
	m->test.harness           = sdsnew("t/h.c");
	add_single_field_deps(m);
	add_odd_bins(m);

	ASSERT(manifest_write(path, m) == 0, "write");
	sds text = read_all(path);
	ASSERT(text != nullptr, "read back");
	ASSERT(strstr(text, "[package]\n\nsources = [\n  \"src/a.c\",\n]\n") == text, "no name, null source skipped");
	ASSERT(strstr(text, "headers = [\n  \"include/a.h\",\n]\n") != nullptr, "null header skipped");
	ASSERT(strstr(text, "dgit = { git = ") != nullptr, "git written first");
	ASSERT(strstr(text, "dver = { version = \"1.0\" }") != nullptr, "version written first");
	ASSERT(strstr(text, "dbranch = { branch = \"main\" }") != nullptr, "branch written first");
	ASSERT(strstr(text, "dtag = { tag = \"v1\" }") != nullptr, "tag written first");
	ASSERT(strstr(text, "drev = { rev = \"abc\" }") != nullptr, "rev written first");
	ASSERT(strstr(text, "dopt = { optional = true }") != nullptr, "optional written first");
	ASSERT(strstr(text, "f = [\"x\", ]") != nullptr, "null feature dep skipped");
	ASSERT(strstr(text, "harness = \"t/h.c\"") != nullptr, "harness without sources");
	ASSERT(strstr(text, "framework") == nullptr, "no framework");
	ASSERT(strstr(text, "name = \"nosrc\"\n\n") != nullptr, "bin without src list");

	manifest_t *back = manifest_parse(path);
	ASSERT(back != nullptr, "written file re-parses");
	ASSERT(back->dependencies.deps_count == 6, "six structured deps");
	ASSERT(back->bin_count == 2, "unnamed bin dropped");
	ASSERT(back->package.sources_count == 1 && back->package.headers_count == 1, "null slots dropped");
	manifest_free(back);
	sdsfree(text);

	/* [test] with only a framework, then with only sources (one null) */
	sdsfree(m->test.harness);
	m->test.harness   = nullptr;
	m->test.framework = sdsnew("unity");
	ASSERT(manifest_write(path, m) == 0, "write framework only");
	text = read_all(path);
	ASSERT(text != nullptr && strstr(text, "[test]\nframework = \"unity\"") != nullptr, "framework only");
	sdsfree(text);

	sdsfree(m->test.framework);
	m->test.framework     = nullptr;
	m->test.sources_count = 2;
	m->test.sources       = sds_array2(nullptr, "t/a.c");
	ASSERT(manifest_write(path, m) == 0, "write sources only");
	text = read_all(path);
	ASSERT(text != nullptr && strstr(text, "[test]\nsources = [\n  \"t/a.c\",\n]\n") != nullptr, "sources only");
	sdsfree(text);

	manifest_free(m);
	sdsfree(path);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* coffee_features.c: null slots and argument checks                  */
/* ------------------------------------------------------------------ */

TEST(covp_features_hand_built_sets)
{
	resolved_features_t rf       = { 0 };
	sds                 names[2] = { nullptr, sdsnew("app") };
	sds                 feats[5] = { nullptr, sdsnew("default"), sdsnew("X1"), sdsnew("fast-mode"), sdsnew("a{b") };
	feature_set_t       sets[2]  = { { 0 }, { feats, 5 } };
	rf.package_names             = names;
	rf.packages                  = sets;
	rf.package_count             = 2;

	sds app = sdsnew("app");
	sds x1  = sdsnew("X1");
	ASSERT(features_is_enabled(&rf, app, x1), "lookup skips null package and feature slots");
	ASSERT(!features_is_enabled(&rf, nullptr, x1), "null package");
	ASSERT(!features_is_enabled(&rf, app, nullptr), "null feature");
	ASSERT(!features_is_enabled(nullptr, app, x1), "null set");

	size_t count = 99;
	ASSERT(features_to_compiler_flags(&rf, nullptr, &count) == nullptr, "null package");
	ASSERT(features_to_compiler_flags(&rf, app, nullptr) == nullptr, "null count");
	sds *flags = features_to_compiler_flags(&rf, app, &count);
	ASSERT(flags != nullptr && count == 3, "null and default skipped");
	ASSERT(strcmp(flags[0], "-DFEATURE_X1") == 0, "upper case and digits kept");
	ASSERT(strcmp(flags[1], "-DFEATURE_FAST_MODE") == 0, "lower case raised, dash replaced");
	ASSERT(strcmp(flags[2], "-DFEATURE_A{B") == 0, "bytes above z kept");

	for (size_t i = 0; i < count; i++) {
		sdsfree(flags[i]);
	}
	safe_free(flags);
	sdsfree(x1);
	sdsfree(app);
	for (size_t i = 1; i < 5; i++) {
		sdsfree(feats[i]);
	}
	sdsfree(names[1]);
	PASS();
}

TEST(covp_features_resolve_null_slots)
{
	manifest_t *m             = safe_calloc(1, sizeof(manifest_t));
	m->package.name           = sdsnew("app");
	m->features_count         = 3;
	m->features               = safe_calloc(3, sizeof(feature_def_t));
	m->features[1].name       = sdsnew("default");
	m->features[1].deps_count = 2;
	m->features[1].deps       = sds_array2(nullptr, "fast");
	m->features[2].name       = sdsnew("fast");

	sds                  req[] = { nullptr, sdsnew("extra") };
	resolved_features_t *rf    = features_resolve(m, req, 2, false, false);
	ASSERT(rf != nullptr, "resolve");
	ASSERT(package_has_feature(rf, "app", "extra"), "requested feature enabled");
	ASSERT(package_has_feature(rf, "app", "fast"), "default dep enabled");
	features_free(rf);

	rf = features_resolve(m, nullptr, 0, true, false);
	ASSERT(rf != nullptr, "resolve all");
	ASSERT(package_has_feature(rf, "app", "default") && package_has_feature(rf, "app", "fast"), "all named features");
	features_free(rf);

	sdsfree(req[1]);
	manifest_free(m);
	PASS();
}

TEST(covp_features_dep_without_package_name)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds deps = sub_path(dir, "deps");
	sds anon = sub_path(dir, "deps/anon");
	ASSERT(mkdir(deps, 0755) == 0 && mkdir(anon, 0755) == 0, "mkdir deps");
	sds p1 = write_file(anon, "Coffee.toml", "[features]\nx = [\"other/y\"]\n");
	ASSERT(p1 != nullptr, "write dep manifest");

	manifest_t *m = parse_manifest_text(dir, "[package]\nname = \"app\"\n\n[features]\nf = [\"anon/x\"]\n");
	ASSERT(m != nullptr, "parse root");

	char *saved_cwd = getcwd(nullptr, 0);
	ASSERT(saved_cwd != nullptr, "getcwd");
	ASSERT(chdir(dir) == 0, "chdir");
	sds                  req[] = { sdsnew("f") };
	resolved_features_t *rf    = features_resolve(m, req, 1, false, true);
	ASSERT(chdir(saved_cwd) == 0, "restore cwd");
	free(saved_cwd);

	ASSERT(rf != nullptr, "resolve");
	ASSERT(package_has_feature(rf, "anon", "x"), "anon/x enabled");
	ASSERT(rf->package_count == 2, "unnamed dep manifest adds no package and no cross refs");

	features_free(rf);
	sdsfree(req[0]);
	manifest_free(m);
	sdsfree(p1);
	sdsfree(anon);
	sdsfree(deps);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* makefile.c: value and token safety                                 */
/* ------------------------------------------------------------------ */

TEST(covp_makefile_target_dir_safety)
{
	static const struct {
		const char *value;
		bool        safe;
	} cases[] = {
		{ "az", true },   { "AZ", true },   { "09", true },   { "a.b", true },  { "a_b", true },  { "a/b", true },
		{ "a+b", true },  { "a@b", true },  { "a=b", true },  { "a-b", true },  { "a*b", true },  { "a?b", true },
		{ "a{b", false }, { "a[b", false }, { "a:b", false }, { "a!b", false }, { "a b", false }, { "a$b", false },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const char *resolved = makefile_resolve_target_dir(cases[i].value);
		const char *expected = cases[i].safe ? cases[i].value : "build";
		ASSERT(strcmp(resolved, expected) == 0, cases[i].value);
	}
	PASS();
}

/* A hand-built manifest named "mk" whose package sources are given. */
static manifest_t *manifest_with_sources(const char **sources, size_t count)
{
	manifest_t *m            = safe_calloc(1, sizeof(manifest_t));
	m->package.name          = sdsnew("mk");
	m->package.sources_count = count;
	m->package.sources       = safe_calloc(count, sizeof(sds));
	for (size_t i = 0; i < count; i++) {
		m->package.sources[i] = sds_or_null(sources[i]);
	}
	return m;
}

/* The SOURCES line of a rendered Makefile, or nullptr. */
static sds render_sources_line(const manifest_t *m, const char *target_dir, bool warn)
{
	sds out = makefile_render(m, nullptr, nullptr, target_dir, false, false, warn);
	if (out == nullptr) {
		return nullptr;
	}
	char *start = strstr(out, "SOURCES :=");
	char *end   = start != nullptr ? strchr(start, '\n') : nullptr;
	sds   line  = nullptr;
	if (end != nullptr) {
		line = sdsnewlen(start, (size_t)(end - start));
	}
	sdsfree(out);
	return line;
}

TEST(covp_makefile_source_token_safety)
{
	static const char *const unsafe[] = {
		"a$b.c",  "a`b.c", "a;b.c",  "a&b.c", "a|b.c", "a>b.c",  "a<b.c",  "a#b.c",
		"a\"b.c", "a'b.c", "a\\b.c", "a(b.c", "a)b.c", "a\nb.c", "a\rb.c", "a\tb.c",
	};

	for (size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); i++) {
		const char *sources[] = { "src/ok.c", unsafe[i] };
		manifest_t *m         = manifest_with_sources(sources, 2);
		sds         line      = render_sources_line(m, nullptr, false);
		ASSERT(line != nullptr && strcmp(line, "SOURCES := src/ok.c") == 0, unsafe[i]);
		sdsfree(line);
		manifest_free(m);
	}

	/* All at once, plus a null slot, a '?' glob and a spaced path: more
	 * entries than the initial list capacity */
	const char *sources[(sizeof(unsafe) / sizeof(unsafe[0])) + 3];
	size_t      n = 0;
	sources[n++]  = nullptr;
	sources[n++]  = "src/a?.c";
	sources[n++]  = "my dir/b.c";
	for (size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); i++) {
		sources[n++] = unsafe[i];
	}
	manifest_t *m    = manifest_with_sources(sources, n);
	sds         line = render_sources_line(m, nullptr, false);
	ASSERT(line != nullptr && strcmp(line, "SOURCES := my\\ dir/b.c $(wildcard src/a?.c)") == 0, "mixed list");
	sdsfree(line);
	manifest_free(m);
	PASS();
}

TEST(covp_makefile_render_source_fallbacks)
{
	/* Only empty sources: nothing to list */
	const char *empty[] = { "" };
	manifest_t *m       = manifest_with_sources(empty, 1);
	sds         line    = render_sources_line(m, "bad dir", true);
	ASSERT(line != nullptr && strcmp(line, "SOURCES :=") == 0, "empty source list");
	sdsfree(line);

	sds out = makefile_render(m, nullptr, nullptr, "bad dir", false, false, false);
	ASSERT(out != nullptr && strstr(out, "TARGET_DIR := build\n") != nullptr, "quiet fallback");
	sdsfree(out);
	out = makefile_render(m, nullptr, nullptr, "bad dir", false, false, true);
	ASSERT(out != nullptr && strstr(out, "TARGET_DIR := build\n") != nullptr, "unsafe target dir falls back");
	sdsfree(out);
	manifest_free(m);

	/* A bin without sources does not replace the default glob */
	m               = safe_calloc(1, sizeof(manifest_t));
	m->package.name = sdsnew("mk");
	m->bin_count    = 1;
	m->bin          = safe_calloc(1, sizeof(binary_target_t));
	m->bin[0].name  = sdsnew("tool");
	line            = render_sources_line(m, nullptr, false);
	ASSERT(line != nullptr && strcmp(line, "SOURCES := $(wildcard src/*.c)") == 0, "default glob");
	sdsfree(line);
	manifest_free(m);
	PASS();
}

TEST(covp_makefile_dep_glob_flags_skipped)
{
	sds dir = make_scratch_dir();
	ASSERT(dir != nullptr, "scratch dir");
	sds deps = sub_path(dir, "deps");
	sds libu = sub_path(dir, "deps/libu");
	ASSERT(mkdir(deps, 0755) == 0 && mkdir(libu, 0755) == 0, "mkdir deps");
	sds p1 = write_file(libu, "library.toml",
	                    "include = [\"inc*\", \"inc?\", \"good\"]\n\n"
	                    "[package]\nname = \"libu\"\nversion = \"1.0.0\"\n");
	ASSERT(p1 != nullptr, "write library.toml");
	manifest_t *m = parse_manifest_text(dir, "dependencies = [\"libu\"]\n\n[package]\nname = \"app\"\n");
	ASSERT(m != nullptr, "parse root");

	char *saved_cwd = getcwd(nullptr, 0);
	ASSERT(saved_cwd != nullptr, "getcwd");
	ASSERT(chdir(dir) == 0, "chdir");
	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(chdir(saved_cwd) == 0, "restore cwd");
	free(saved_cwd);
	ASSERT(g != nullptr, "graph created");

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, false);
	ASSERT(out != nullptr, "render");
	ASSERT(strstr(out, "CFLAGS += -Ideps/libu/good\n") != nullptr, "safe include kept");
	ASSERT(strstr(out, "inc*") == nullptr && strstr(out, "inc?") == nullptr, "glob flags skipped");
	ASSERT(strstr(out, "LDLIBS := -llibu\n") != nullptr, "prebuilt dep linked");

	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	sdsfree(p1);
	sdsfree(libu);
	sdsfree(deps);
	drop_scratch_dir(dir);
	PASS();
}

/* ------------------------------------------------------------------ */
/* Registration                                                       */
/* ------------------------------------------------------------------ */

void coffee_register_cov_parsers_tests(void)
{
	TEST_REGISTER(covp_version_parse_edges);
	TEST_REGISTER(covp_version_satisfies_edges);
	TEST_REGISTER(covp_lockfile_parse_invalid_toml);
	TEST_REGISTER(covp_lockfile_parse_mixed_dependencies);
	TEST_REGISTER(covp_lockfile_write_roundtrip);
	TEST_REGISTER(covp_lockfile_write_rename_failure);
	TEST_REGISTER(covp_lockfile_write_flush_failure);
	TEST_REGISTER(covp_manifest_parse_invalid_toml);
	TEST_REGISTER(covp_manifest_parse_feature_edge_cases);
	TEST_REGISTER(covp_manifest_parse_dependency_arrays);
	TEST_REGISTER(covp_manifest_parse_test_and_compilation);
	TEST_REGISTER(covp_manifest_write_all_fields);
	TEST_REGISTER(covp_manifest_write_failures);
	TEST_REGISTER(covp_manifest_free_and_remove_edges);
	TEST_REGISTER(covp_manifest_extract_dep_info_trailing_space);
	TEST_REGISTER(covp_manifest_parse_non_string_elements);
	TEST_REGISTER(covp_features_unnamed_root);
	TEST_REGISTER(covp_features_shared_foreign_package);
	TEST_REGISTER(covp_features_default_skipped_in_flags);
	TEST_REGISTER(covp_features_transitive_resolution);
	TEST_REGISTER(covp_makefile_render_unsafe_values);
	TEST_REGISTER(covp_makefile_render_bin_sources);
	TEST_REGISTER(covp_makefile_generate_errors);
	TEST_REGISTER(covp_makefile_generate_with_lockfile);
	TEST_REGISTER(covp_makefile_generate_write_failures);
	TEST_REGISTER(covp_staging_fsync_failure);
	TEST_REGISTER(covp_dep_graph_transitive_self_dependency);
	TEST_REGISTER(covp_cmdline_help_exits_zero);
	TEST_REGISTER(covp_cmdline_version_exits_zero);
	TEST_REGISTER(covp_cmdline_null_args_rejected);
	TEST_REGISTER(covp_cmdline_jobs_out_of_range);
	TEST_REGISTER(covp_version_satisfies_more_edges);
	TEST_REGISTER(covp_lockfile_parse_empty_dependencies);
	TEST_REGISTER(covp_lockfile_same_deps_fields);
	TEST_REGISTER(covp_lockfile_same_deps_shape);
	TEST_REGISTER(covp_manifest_parse_odd_sections);
	TEST_REGISTER(covp_manifest_remove_structured_past_unnamed);
	TEST_REGISTER(covp_manifest_extract_dep_info_edges);
	TEST_REGISTER(covp_manifest_write_sparse_fields);
	TEST_REGISTER(covp_features_hand_built_sets);
	TEST_REGISTER(covp_features_resolve_null_slots);
	TEST_REGISTER(covp_features_dep_without_package_name);
	TEST_REGISTER(covp_makefile_target_dir_safety);
	TEST_REGISTER(covp_makefile_source_token_safety);
	TEST_REGISTER(covp_makefile_render_source_fallbacks);
	TEST_REGISTER(covp_makefile_dep_glob_flags_skipped);
}
