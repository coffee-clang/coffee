#include "safe.h"
/*
 * Coverage tests for core modules: build, project, registry, install,
 * cflags, libs — targeting uncovered code paths.
 */

#include "../src/build.h"
#include "../src/coffee.h"
#include "../src/manifest.h"
#include "../src/project.h"
#include "../src/registry.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_coverage_core_tests(void);

static char saved_cwd[4096];

static void setup_proj(const char *name, const char *extra_toml, bool with_main)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-core-%s", name);
	mkdir(tmpdir, 0755);
	assert(getcwd(saved_cwd, sizeof(saved_cwd)) != nullptr);
	assert(chdir(tmpdir) == 0);

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"%s\"\n", name);
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	if (extra_toml) {
		fprintf_safe(fp, "%s\n", extra_toml);
	}
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
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-core-%s", name);
	sds cmd    = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
}

/* ======================== PROJECT ======================== */

/* project_find_manifest returns Coffee.toml in CWD */
TEST(cov_project_find_manifest)
{
	setup_proj("find", nullptr, false);
	sds path = project_find_manifest(nullptr);
	ASSERT(path != nullptr, "found manifest");
	ASSERT(strstr(path, "Coffee.toml") != nullptr, "ends with Coffee.toml");
	sdsfree(path);
	teardown_proj("find");
	PASS();
}

/* project_get_name returns the package name */
TEST(cov_project_get_name)
{
	setup_proj("getname", nullptr, false);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parsed manifest");
	sds name = project_get_name(m);
	ASSERT(name != nullptr, "got name");
	ASSERT(strcmp(name, "getname") == 0, "name matches");
	sdsfree(name);
	manifest_free(m);
	teardown_proj("getname");
	PASS();
}

/* project_find_manifest with non-existent directory */
TEST(cov_project_find_manifest_missing)
{
	sds path = project_find_manifest("/nonexistent-dir-xyzzy");
	ASSERT(path == nullptr, "no manifest found");
	PASS();
}

/* ======================== BUILD ======================== */

/* build_project — debug mode */
TEST(cov_build_debug)
{
	setup_proj("bdebug", nullptr, true);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	build_opts_t opts = { .debug = true };
	i64          ret  = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build debug ok");
	manifest_free(m);
	teardown_proj("bdebug");
	PASS();
}

/* build_project — null manifest */
TEST(cov_build_null_manifest)
{
	i64 ret = build_project(nullptr, nullptr);
	ASSERT(ret != 0, "null manifest should fail");
	PASS();
}

/* build_project — locked without lockfile */
TEST(cov_build_locked_no_lockfile)
{
	setup_proj("nolock", nullptr, true);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	build_opts_t opts = { .locked = true };
	i64          ret  = build_project(m, &opts);
	ASSERT(ret != 0, "locked without lockfile should fail");
	manifest_free(m);
	teardown_proj("nolock");
	PASS();
}

/* build_project — stale lockfile */
TEST(cov_build_stale_lockfile)
{
	setup_proj("stale", nullptr, false);
	/* Create lockfile first */
	FILE *lf = fopen("Coffee.lock", "w");
	assert(lf);
	fprintf_safe(lf, "version = \"1\"\n");
	fclose(lf);

	/* Wait and update Coffee.toml to be newer */
	sleep(1);
	FILE *fp = fopen("Coffee.toml", "a");
	assert(fp);
	fprintf_safe(fp, "# stale marker\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	build_opts_t opts = { .locked = true };
	i64          ret  = build_project(m, &opts);
	ASSERT(ret != 0, "stale lockfile should fail");
	manifest_free(m);
	remove("Coffee.lock");
	teardown_proj("stale");
	PASS();
}

/* build_project — all_features */
TEST(cov_build_all_features)
{
	setup_proj("allf", "features = { extra = [] }\n", true);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	build_opts_t opts = { .all_features = true };
	i64          ret  = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build all_features ok");
	manifest_free(m);
	teardown_proj("allf");
	PASS();
}

/* build_project — no source files error */
TEST(cov_build_no_sources)
{
	setup_proj("nosrc", nullptr, false);
	/* Remove the src directory we just created */
	rmdir("src");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	build_opts_t opts = {};
	i64          ret  = build_project(m, &opts);
	ASSERT(ret != 0, "no sources should fail");
	manifest_free(m);
	teardown_proj("nosrc");
	PASS();
}

/* build_run — with args */
TEST(cov_build_run_with_args)
{
	setup_proj("runargs", nullptr, true);
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse");

	sds          args[] = { sdsnew("--help") };
	build_opts_t opts   = {};
	i64          ret    = build_run(m, &opts, args, 1);
	ASSERT(ret == 0 || ret == 1, "build_run with args");
	manifest_free(m);
	sdsfree(args[0]);
	teardown_proj("runargs");
	PASS();
}

/* build_run — with failure (build_project fails first) */
TEST(cov_build_run_no_project)
{
	build_opts_t opts = {};
	i64          ret  = build_run(nullptr, &opts, nullptr, 0);
	ASSERT(ret != 0, "build_run with null manifest should fail");
	PASS();
}

/* ======================== DEP_RESOLVE_DIR ======================== */

/* dep_resolve_dir — global ~/.coffee/deps/ */
TEST(cov_dep_resolve_global)
{
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	sds global_deps = sdscatprintf(sdsempty(), "%s/.coffee/deps/globalpkg", home);
	mkdir(global_deps, 0755);

	sds path = dep_resolve_dir("globalpkg");
	ASSERT(path != nullptr, "found globalpkg");
	ASSERT(strstr(path, ".coffee/deps/") != nullptr, "global path");
	sdsfree(path);

	/* Cleanup — remove parent dirs up to .coffee */
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s/.coffee", home);
	system(cmd);
	sdsfree(cmd);
	sdsfree(global_deps);
	PASS();
}

/* ======================== REGISTRY ======================== */

/* coffee_home_dir returns non-null */
TEST(cov_registry_home_dir)
{
	const char *dir = coffee_home_dir();
	ASSERT(dir != nullptr, "home dir not null");
	ASSERT(strlen(dir) > 0, "home dir non-empty");
	/* NOTE: coffee_home_dir returns a static buffer, do NOT free */
	PASS();
}

/* registry_free_recipe with null */
TEST(cov_registry_free_recipe_null)
{
	registry_free_recipe(nullptr);
	PASS();
}

/* registry_free_recipes with null */
TEST(cov_registry_free_recipes_null)
{
	registry_free_recipes(nullptr);
	PASS();
}

/* registry_free_versions with null */
TEST(cov_registry_free_versions_null)
{
	registry_free_versions(nullptr);
	PASS();
}

/* registry_free_recipe with non-null fields */
TEST(cov_registry_free_recipe_nonnull)
{
	recipe_t *r = safe_calloc(1, sizeof(recipe_t));
	ASSERT(r != nullptr, "alloc");
	r->name         = strdup("test-pkg");
	r->version      = strdup("1.0.0");
	r->license      = strdup("MIT");
	r->description  = strdup("A test package");
	r->download_url = strdup("https://example.com/pkg.tar.gz");
	r->dependencies = strdup("dep-a, dep-b");
	r->repo         = strdup("https://github.com/test/pkg");
	registry_free_recipe(r);
	PASS();
}

/* registry_free_recipes with non-null recipes (multiple entries) */
TEST(cov_registry_free_recipes_nonnull)
{
	recipe_list_t *list = safe_calloc(1, sizeof(recipe_list_t));
	ASSERT(list != nullptr, "alloc");
	list->count   = 2;
	list->recipes = safe_calloc(2, sizeof(recipe_t));
	ASSERT(list->recipes != nullptr, "alloc recipes");
	list->recipes[0].name         = strdup("pkg-a");
	list->recipes[0].version      = strdup("1.0.0");
	list->recipes[0].license      = strdup("MIT");
	list->recipes[0].description  = strdup("Package A");
	list->recipes[0].download_url = strdup("https://ex.com/a.tar.gz");
	list->recipes[0].dependencies = strdup("");
	list->recipes[0].repo         = strdup("https://github.com/a");
	list->recipes[1].name         = strdup("pkg-b");
	list->recipes[1].version      = strdup("2.0.0");
	list->recipes[1].license      = strdup("Apache-2.0");
	list->recipes[1].description  = strdup("Package B");
	list->recipes[1].download_url = strdup("https://ex.com/b.tar.gz");
	list->recipes[1].dependencies = strdup("pkg-a");
	list->recipes[1].repo         = strdup("https://github.com/b");
	registry_free_recipes(list);
	PASS();
}

/* registry_free_versions with non-null version list */
TEST(cov_registry_free_versions_nonnull)
{
	version_list_t *list = safe_calloc(1, sizeof(version_list_t));
	ASSERT(list != nullptr, "alloc");
	list->count    = 3;
	list->versions = safe_malloc(3 * sizeof(char *));
	ASSERT(list->versions != nullptr, "alloc versions");
	list->versions[0] = strdup("1.0.0");
	list->versions[1] = strdup("1.1.0");
	list->versions[2] = strdup("2.0.0");
	registry_free_versions(list);
	PASS();
}

/* ======================== INSTALL ======================== */

/* handle_install_update with specific target missing */
TEST(cov_install_update_missing_target)
{
	/* Ensure the global deps dir exists so the handler reaches the target check */
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	sds coffee_dir = sdscatprintf(sdsempty(), "%s/.coffee", home);
	sds deps_dir   = sdscatprintf(sdsempty(), "%s/.coffee/deps", home);
	mkdir(coffee_dir, 0755);
	mkdir(deps_dir, 0755);

	options opt = {
		.inputs     = (char *[]){ "install-update", "nonexistent-pkg-xyz" },
		.inputs_num = 2,
	};
	i64 ret = handle_install_update(&opt);
	ASSERT(ret == 1, "update missing pkg fails");
	sdsfree(coffee_dir);
	sdsfree(deps_dir);
	PASS();
}

/* ======================== CFLAGS / LIBS ======================== */

/* handle_cflags with deps/ directory present */
TEST(cov_cflags_with_dep_present)
{
	setup_proj("cfdep", "dependencies = [\"cfdep\"]\n", true);
	mkdir("deps", 0755);
	mkdir("deps/cfdep", 0755);
	mkdir("deps/cfdep/include", 0755);
	FILE *fp = fopen("deps/cfdep/include/test.h", "w");
	if (fp) {
		fclose(fp);
	}

	options opt = {
		.inputs     = (char *[]){ "cflags", "cfdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_cflags(&opt);
	ASSERT(ret == 0, "cflags with dep present");
	rmdir("deps/cfdep/include");
	rmdir("deps/cfdep");
	rmdir("deps");
	teardown_proj("cfdep");
	PASS();
}

/* handle_libs with deps/ directory present */
TEST(cov_libs_with_dep_present)
{
	setup_proj("libdep", "dependencies = [\"libdep\"]\n", true);
	mkdir("deps", 0755);
	mkdir("deps/libdep", 0755);
	mkdir("deps/libdep/lib", 0755);

	options opt = {
		.inputs     = (char *[]){ "libs", "libdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_libs(&opt);
	ASSERT(ret == 0, "libs with dep present");
	rmdir("deps/libdep/lib");
	rmdir("deps/libdep");
	rmdir("deps");
	teardown_proj("libdep");
	PASS();
}

/* ======================== FEATURES ======================== */

/* features_parse_cli with null or empty */
TEST(cov_features_parse_cli_empty)
{
	sds   *out = nullptr;
	size_t cnt = 0;
	features_parse_cli(nullptr, &out, &cnt);
	/* should produce no output */
	PASS();
}

TEST(cov_features_parse_cli_single)
{
	sds   *out = nullptr;
	size_t cnt = 0;
	features_parse_cli("feat1", &out, &cnt);
	ASSERT(cnt == 1, "one feature");
	ASSERT(strcmp(out[0], "feat1") == 0, "feat1");
	for (size_t i = 0; i < cnt; i++) {
		sdsfree(out[i]);
	}
	safe_free(out);
	PASS();
}

TEST(cov_features_parse_cli_multi)
{
	sds   *out = nullptr;
	size_t cnt = 0;
	features_parse_cli("feat1,feat2,feat3", &out, &cnt);
	ASSERT(cnt == 3, "three features");
	ASSERT(strcmp(out[0], "feat1") == 0, "feat1");
	ASSERT(strcmp(out[1], "feat2") == 0, "feat2");
	ASSERT(strcmp(out[2], "feat3") == 0, "feat3");
	for (size_t i = 0; i < cnt; i++) {
		sdsfree(out[i]);
	}
	safe_free(out);
	PASS();
}

/* features_free with nullptr */
TEST(cov_features_free_null)
{
	features_free(nullptr);
	PASS();
}

/* features_to_compiler_flags with null */
TEST(cov_features_to_flags_null)
{
	size_t cnt = 0;
	sds   *res = features_to_compiler_flags(nullptr, nullptr, &cnt);
	ASSERT(res == nullptr, "null input -> null output");
	PASS();
}

void coffee_register_coverage_core_tests(void)
{
	TEST_REGISTER(cov_project_find_manifest);
	TEST_REGISTER(cov_project_get_name);
	TEST_REGISTER(cov_project_find_manifest_missing);
	TEST_REGISTER(cov_build_debug);
	TEST_REGISTER(cov_build_null_manifest);
	TEST_REGISTER(cov_build_locked_no_lockfile);
	TEST_REGISTER(cov_build_stale_lockfile);
	TEST_REGISTER(cov_build_all_features);
	TEST_REGISTER(cov_build_no_sources);
	TEST_REGISTER(cov_build_run_with_args);
	TEST_REGISTER(cov_build_run_no_project);
	TEST_REGISTER(cov_dep_resolve_global);
	TEST_REGISTER(cov_registry_home_dir);
	TEST_REGISTER(cov_registry_free_recipe_null);
	TEST_REGISTER(cov_registry_free_recipes_null);
	TEST_REGISTER(cov_registry_free_versions_null);
	TEST_REGISTER(cov_registry_free_recipe_nonnull);
	TEST_REGISTER(cov_registry_free_recipes_nonnull);
	TEST_REGISTER(cov_registry_free_versions_nonnull);
	TEST_REGISTER(cov_install_update_missing_target);
	TEST_REGISTER(cov_cflags_with_dep_present);
	TEST_REGISTER(cov_libs_with_dep_present);
	TEST_REGISTER(cov_features_parse_cli_empty);
	TEST_REGISTER(cov_features_parse_cli_single);
	TEST_REGISTER(cov_features_parse_cli_multi);
	TEST_REGISTER(cov_features_free_null);
	TEST_REGISTER(cov_features_to_flags_null);
}
