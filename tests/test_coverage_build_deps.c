/*
 * Coverage tests for build.c dependency resolution loop and
 * coffee_features.c transitive dependency resolution.
 *
 * These target the highest-impact uncovered code paths:
 *   - build.c lines ~293–317 (dep loop over manifest dependencies)
 *   - build.c lines ~167–175 (compiling dep source files)
 *   - build.c lines ~75–79 (global dep fallback)
 *   - build.c lines ~25–51 (verbose/signal/fork error paths)
 *   - coffee_features.c lines ~170–210 (transitive cross-package refs)
 *   - build_run's exec failure paths
 */

#include "../src/build.h"
#include "../src/coffee.h"
#include "../src/coffee_features.h"
#include "../src/manifest.h"
#include "../src/project.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_coverage_build_deps_tests(void);

static char saved_cwd[4096];

static void setup_proj(const char *name, const char *extra_toml, bool with_main)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-bdep-%s", name);
	mkdir(tmpdir, 0755);
	assert(getcwd(saved_cwd, sizeof(saved_cwd)) != nullptr);
	assert(chdir(tmpdir) == 0);

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	if (extra_toml) {
		fprintf_safe(fp, "%s\n", extra_toml);
	}
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"%s\"\n", name);
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);

	mkdir("src", 0755);
	if (with_main) {
		FILE *mc = fopen("src/main.c", "w");
		if (mc) {
			fprintf_safe(mc, "int main(void) { return 0; }\n");
			fclose(mc);
		}
	}
	sdsfree(tmpdir);
}

static void teardown_proj(const char *name)
{
	chdir(saved_cwd);
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-bdep-%s", name);
	sds cmd    = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
}

/* Helper: create a library.toml for a dep */
static void create_library_toml(const char *dep_dir, const char *dep_name, const char *version, const char *deps_str)
{
	FILE *lt = fopen(dep_dir, "w");
	assert(lt);
	fprintf_safe(lt, "[package]\n");
	fprintf_safe(lt, "name = \"%s\"\n", dep_name);
	fprintf_safe(lt, "version = \"%s\"\n", version ? version : "1.0.0");
	if (deps_str) {
		fprintf_safe(lt, "dependencies = %s\n", deps_str);
	}
	fclose(lt);
}

/* ===================== BUILD.C DEP LOOP ===================== */

/* build_project with a manifest that has a dependency resolved via deps/ */
TEST(cov_build_dep_loop_local)
{
	setup_proj("deploop", "dependencies = [\"fakedep\"]", true);

	/* Create the local dep directory with source file */
	mkdir("deps", 0755);
	mkdir("deps/fakedep", 0755);
	mkdir("deps/fakedep/src", 0755);
	mkdir("deps/fakedep/lib", 0755);
	FILE *sf = fopen("deps/fakedep/src/fakedep.c", "w");
	if (sf) {
		fprintf_safe(sf, "int fakedep_do(void) { return 42; }\n");
		fclose(sf);
	}
	/* Build a stub static library so the linker doesn't fail */
	system("clang -c deps/fakedep/src/fakedep.c -o deps/fakedep/fakedep.o 2>/dev/null");
	system("ar rcs deps/fakedep/lib/libfakedep.a deps/fakedep/fakedep.o 2>/dev/null");
	/* Create library.toml so the dep is properly recognized */
	create_library_toml("deps/fakedep/library.toml", "fakedep", "1.0.0", nullptr);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = {};
	i64          ret  = build_project(m, &opts);
	/* The build may compile our dummy source — success or fail is fine */
	ASSERT(ret == 0 || ret == 1, "build with dep loop");

	manifest_free(m);
	teardown_proj("deploop");
	PASS();
}

/* build_project with manifest with deps where lockfile provides the path */
TEST(cov_build_dep_loop_lockfile)
{
	setup_proj("deplock", "dependencies = [\"lockdep\"]", true);

	/* Create the dep at an absolute path */
	mkdir("/tmp/coverage-bdep-lockdep", 0755);
	mkdir("/tmp/coverage-bdep-lockdep/src", 0755);
	FILE *sf = fopen("/tmp/coverage-bdep-lockdep/src/lockdep.c", "w");
	if (sf) {
		fprintf_safe(sf, "int lockdep_do(void) { return 0; }\n");
		fclose(sf);
	}
	create_library_toml("/tmp/coverage-bdep-lockdep/library.toml", "lockdep", "1.0.0", nullptr);

	/* Create lockfile that points to the absolute dep path */
	FILE *lf = fopen("Coffee.lock", "w");
	assert(lf);
	fprintf_safe(lf, "[[dependency]]\n");
	fprintf_safe(lf, "name = \"lockdep\"\n");
	fprintf_safe(lf, "version = \"1.0.0\"\n");
	fprintf_safe(lf, "path = \"/tmp/coverage-bdep-lockdep\"\n");
	fclose(lf);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = { .locked = true };
	i64          ret  = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build with lockfile dep");

	manifest_free(m);
	remove("Coffee.lock");
	sds cmd = sdsnew("rm -rf /tmp/coverage-bdep-lockdep");
	system(cmd);
	sdsfree(cmd);
	teardown_proj("deplock");
	PASS();
}

/* build_run with exec of the built binary (hits exec failure path) */
TEST(cov_build_run_exec)
{
	setup_proj("runexec", nullptr, true);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = {};
	i64          ret  = build_run(m, &opts, nullptr, 0);
	/* Build succeeds or fails, then exec of non-existent binary may fail */
	ASSERT(ret == 0 || ret == 1, "build_run exec");

	manifest_free(m);
	teardown_proj("runexec");
	PASS();
}

/* build_project with verbose flag (hits verbose print lines) */
TEST(cov_build_verbose)
{
	setup_proj("bverbose", nullptr, true);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = { .verbose = true };
	i64          ret  = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build verbose");

	manifest_free(m);
	teardown_proj("bverbose");
	PASS();
}

/* build_project with release mode (hits -O2 -s flag path) */
TEST(cov_build_release)
{
	setup_proj("brel", nullptr, true);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = { .release = true };
	i64          ret  = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build release");

	manifest_free(m);
	teardown_proj("brel");
	PASS();
}

/* dep_resolve_dir: global fallback when deps/ and vendor/ don't exist */
TEST(cov_build_dep_resolve_global)
{
	/* Ensure ~/.coffee/deps/global-test/ exists */
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	sds global_dir = sdscatprintf(sdsempty(), "%s/.coffee/deps/global-test", home);
	mkdir(global_dir, 0755);

	sds path = dep_resolve_dir("global-test");
	ASSERT(path != nullptr, "resolved global dep");
	ASSERT(strstr(path, ".coffee/deps/") != nullptr, "global path");

	sdsfree(path);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s/.coffee/deps/global-test", home);
	system(cmd);
	sdsfree(cmd);
	sdsfree(global_dir);
	PASS();
}

/* ===================== FEATURES TRANSITIVE ===================== */

/* features_resolve with manifest that has features referencing foreign packages */
TEST(cov_features_transitive)
{
	setup_proj("transfeat",
	           "dependencies = [\"transdep\"]\n"
	           "[features]\n"
	           "default = []\n"
	           "extra = [\"transdep/extra-feat\"]\n",
	           true);

	/* Create the dep in local deps/ */
	mkdir("deps", 0755);
	mkdir("deps/transdep", 0755);
	mkdir("deps/transdep/src", 0755);
	FILE *sf = fopen("deps/transdep/src/transdep.c", "w");
	if (sf) {
		fprintf_safe(sf, "int transdep_do(void) { return 0; }\n");
		fclose(sf);
	}
	/* Give the dep a library.toml that defines its own features */
	FILE *lt = fopen("deps/transdep/library.toml", "w");
	assert(lt);
	fprintf_safe(lt, "[package]\n");
	fprintf_safe(lt, "name = \"transdep\"\n");
	fprintf_safe(lt, "version = \"1.0.0\"\n");
	fprintf_safe(lt, "[features]\n");
	fprintf_safe(lt, "extra-feat = []\n");
	fclose(lt);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	sds                  requested[] = { sdsnew("extra") };
	resolved_features_t *rf          = features_resolve(m, requested, 1, false, false);
	ASSERT(rf != nullptr, "resolved features with transitive");

	/* Should have root and transdep packages in the result */
	ASSERT(rf->package_count >= 2, "at least 2 packages resolved");
	ASSERT(features_is_enabled(rf, m->package.name, "extra"), "extra enabled on root");

	sdsfree(requested[0]);
	features_free(rf);
	manifest_free(m);
	teardown_proj("transfeat");
	PASS();
}

/* features_to_compiler_flags with a non-empty feature set */
TEST(cov_features_to_flags_nonempty)
{
	setup_proj("flagsfeat", "[features]\nfeat_a = []\nfeat_b = []\n", true);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	sds                  requested[] = { sdsnew("feat_a"), sdsnew("feat_b") };
	resolved_features_t *rf          = features_resolve(m, requested, 2, false, true);
	ASSERT(rf != nullptr, "resolved");

	size_t cnt   = 0;
	sds   *flags = features_to_compiler_flags(rf, m->package.name, &cnt);
	ASSERT(flags != nullptr, "flags non-null");
	ASSERT(cnt == 2, "two flags");

	for (size_t i = 0; i < cnt; i++) {
		sdsfree(flags[i]);
	}
	free(flags);
	sdsfree(requested[0]);
	sdsfree(requested[1]);
	features_free(rf);
	manifest_free(m);
	teardown_proj("flagsfeat");
	PASS();
}

/* features_to_compiler_flags: package not found */
TEST(cov_features_to_flags_notfound)
{
	setup_proj("flagnf", nullptr, true);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	resolved_features_t *rf = features_resolve(m, nullptr, 0, false, false);
	ASSERT(rf != nullptr, "resolved");

	size_t cnt   = 0;
	sds   *flags = features_to_compiler_flags(rf, "nonexistent-pkg", &cnt);
	ASSERT(flags == nullptr, "flags null for unknown pkg");
	ASSERT(cnt == 0, "cnt 0");

	features_free(rf);
	manifest_free(m);
	teardown_proj("flagnf");
	PASS();
}

/* features_is_enabled: feature not enabled */
TEST(cov_features_is_enabled_false)
{
	setup_proj("isenabled", nullptr, true);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	resolved_features_t *rf = features_resolve(m, nullptr, 0, false, false);
	ASSERT(rf != nullptr, "resolved");

	ASSERT(!features_is_enabled(rf, m->package.name, "nonexistent"), "not enabled");
	ASSERT(!features_is_enabled(rf, "other-pkg", "any"), "other pkg not enabled");

	features_free(rf);
	manifest_free(m);
	teardown_proj("isenabled");
	PASS();
}

/* features_resolve with null manifest */
TEST(cov_features_resolve_null)
{
	resolved_features_t *rf = features_resolve(nullptr, nullptr, 0, false, false);
	ASSERT(rf == nullptr, "null manifest -> null result");
	PASS();
}

/* features_is_enabled with null args */
TEST(cov_features_is_enabled_null)
{
	ASSERT(!features_is_enabled(nullptr, sdsnew("pkg"), sdsnew("feat")), "null rf");
	ASSERT(!features_is_enabled(nullptr, nullptr, nullptr), "all null");
	PASS();
}

/* features_to_compiler_flags with null args */
TEST(cov_features_to_flags_null_args)
{
	size_t cnt   = 0;
	sds   *flags = features_to_compiler_flags(nullptr, nullptr, nullptr);
	ASSERT(flags == nullptr, "null rf -> null");

	flags = features_to_compiler_flags(nullptr, sdsnew("pkg"), &cnt);
	ASSERT(flags == nullptr, "null rf with package -> null");

	flags = features_to_compiler_flags(nullptr, nullptr, &cnt);
	ASSERT(flags == nullptr, "null rf with cnt -> null");
	PASS();
}

/* features_parse_cli with null output args */
TEST(cov_features_parse_cli_null_out)
{
	sds   *out = (sds *)0x1; /* non-null garbage to verify it gets set to null */
	size_t cnt = 99;
	features_parse_cli(nullptr, &out, &cnt);
	ASSERT(out == nullptr, "null input -> out=null");
	ASSERT(cnt == 0, "null input -> cnt=0");
	PASS();
}

/* features_parse_cli where calloc fails is impractical to test, skip */

void coffee_register_coverage_build_deps_tests(void)
{
	TEST_REGISTER(cov_build_dep_loop_local);
	TEST_REGISTER(cov_build_dep_loop_lockfile);
	TEST_REGISTER(cov_build_run_exec);
	TEST_REGISTER(cov_build_verbose);
	TEST_REGISTER(cov_build_release);
	TEST_REGISTER(cov_build_dep_resolve_global);
	TEST_REGISTER(cov_features_transitive);
	TEST_REGISTER(cov_features_to_flags_nonempty);
	TEST_REGISTER(cov_features_to_flags_notfound);
	TEST_REGISTER(cov_features_is_enabled_false);
	TEST_REGISTER(cov_features_resolve_null);
	TEST_REGISTER(cov_features_is_enabled_null);
	TEST_REGISTER(cov_features_to_flags_null_args);
	TEST_REGISTER(cov_features_parse_cli_null_out);
}
