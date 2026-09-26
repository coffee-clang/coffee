#include "../src/registry.h"
#include "safe.h"
/*
 * Coverage tests for build.c dependency resolution and
 * coffee_features.c transitive dependency resolution.
 *
 * These target the highest-impact uncovered code paths:
 *   - build.c global dep fallback
 *   - coffee_features.c transitive cross-package refs
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

static char saved_cwd[4'096];

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
	test_remove_tree(tmpdir);
	sdsfree(tmpdir);
}

/* ===================== DEP RESOLVE ===================== */

/* dep_resolve_dir: global fallback when deps/ and vendor/ don't exist */
TEST(cov_build_dep_resolve_global)
{
	/* Ensure the sandboxed global deps dir exists */
	sds global_dir = sdscatprintf(sdsempty(), "%s/deps/global-test", coffee_home_dir());
	mkdir(global_dir, 0755);

	sds path = dep_resolve_dir("global-test");
	ASSERT(path != nullptr, "resolved global dep");
	ASSERT(strstr(path, "/deps/global-test") != nullptr, "global path");

	sdsfree(path);
	sds path2 = sdscatprintf(sdsempty(), "%s/deps/global-test", coffee_home_dir());
	test_remove_tree(path2);
	sdsfree(path2);
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
	safe_free(flags);
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
	sds pkg  = sdsnew("pkg");
	sds feat = sdsnew("feat");
	ASSERT(!features_is_enabled(nullptr, pkg, feat), "null rf");
	ASSERT(!features_is_enabled(nullptr, nullptr, nullptr), "all null");
	sdsfree(pkg);
	sdsfree(feat);
	PASS();
}

/* features_to_compiler_flags with null args */
TEST(cov_features_to_flags_null_args)
{
	size_t cnt   = 0;
	sds   *flags = features_to_compiler_flags(nullptr, nullptr, nullptr);
	ASSERT(flags == nullptr, "null rf -> null");

	sds pkg2 = sdsnew("pkg");
	flags    = features_to_compiler_flags(nullptr, pkg2, &cnt);
	sdsfree(pkg2);
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
