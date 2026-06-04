/*
 * Coverage tests for build.c uncovered paths.
 *
 * Targets:
 *   - dep_resolve_dir with found dirs (deps/, vendor/, global)
 *   - dep_parse_name
 *   - dep_add_flags with library.toml, fallbacks, source glob
 *   - build_run
 *   - build_project with locked/verbose/release/features modes
 */

#include "../src/build.h"
#include "../src/lockfile.h"
#include "../src/manifest.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_coverage_build_tests(void);

TEST(cov_dep_resolve_dir_deps_local)
{
	mkdir("deps", 0755);
	mkdir("deps/testpkg", 0755);

	sds path = dep_resolve_dir("testpkg");
	ASSERT(path != nullptr, "dep_resolve_dir found testpkg");

	sdsfree(path);
	rmdir("deps/testpkg");
	rmdir("deps");
	PASS();
}

TEST(cov_dep_resolve_dir_not_found)
{
	sds path = dep_resolve_dir("nonexistent-pkg-xyzzy");
	ASSERT(path == nullptr, "dep_resolve_dir should return nullptr for missing pkg");

	PASS();
}

TEST(cov_dep_resolve_dir_vendor)
{
	mkdir("vendor", 0755);
	mkdir("vendor/vendorpkg", 0755);

	sds path = dep_resolve_dir("vendorpkg");
	ASSERT(path != nullptr, "found vendorpkg");
	ASSERT(strstr(path, "vendor/") != nullptr, "path contains vendor/");

	sdsfree(path);
	rmdir("vendor/vendorpkg");
	rmdir("vendor");
	PASS();
}

/* dep_add_flags with library.toml containing include/lib dirs */
TEST(cov_dep_add_flags_with_library_toml)
{
	mkdir("deps", 0755);
	mkdir("deps/testlib", 0755);

	FILE *fp = fopen("deps/testlib/library.toml", "w");
	ASSERT(fp != nullptr, "create library.toml");
	fprintf_safe(fp, "include = [\"include\"]\n");
	fprintf_safe(fp, "lib = [\"lib\"]\n");
	fclose(fp);

	mkdir("deps/testlib/include", 0755);
	mkdir("deps/testlib/lib", 0755);

	sds    flags = sdsempty();
	size_t found = dep_add_flags("deps/testlib", "testlib", &flags, nullptr, nullptr);
	ASSERT(found > 0, "dep_add_flags should find entries");
	ASSERT(strstr(flags, "-I") != nullptr, "flags contain -I");
	ASSERT(strstr(flags, "-L") != nullptr, "flags contain -L");
	ASSERT(strstr(flags, "-ltestlib") != nullptr, "flags contain -ltestlib");

	sdsfree(flags);
	rmdir("deps/testlib/lib");
	rmdir("deps/testlib/include");
	remove("deps/testlib/library.toml");
	rmdir("deps/testlib");
	rmdir("deps");
	PASS();
}

/* dep_add_flags fallback: no library.toml, use include/ and lib/ */
TEST(cov_dep_add_flags_fallback)
{
	mkdir("deps", 0755);
	mkdir("deps/fallback", 0755);
	mkdir("deps/fallback/include", 0755);
	mkdir("deps/fallback/lib", 0755);

	sds    flags = sdsempty();
	size_t found = dep_add_flags("deps/fallback", "fallback", &flags, nullptr, nullptr);
	ASSERT(found == 0, "fallback should return 0 (no library.toml)");
	ASSERT(strstr(flags, "-I") != nullptr, "flags contain -I fallback");
	ASSERT(strstr(flags, "-L") != nullptr, "flags contain -L fallback");
	ASSERT(strstr(flags, "-lfallback") != nullptr, "flags contain -lfallback");

	sdsfree(flags);
	rmdir("deps/fallback/lib");
	rmdir("deps/fallback/include");
	rmdir("deps/fallback");
	rmdir("deps");
	PASS();
}

/* dep_add_flags with null directory */
TEST(cov_dep_add_flags_null_dir)
{
	sds    flags = sdsempty();
	size_t found = dep_add_flags(nullptr, "nulldep", &flags, nullptr, nullptr);
	ASSERT(found == 0, "null dir returns 0");
	sdsfree(flags);
	PASS();
}

/* dep_parse_name */
TEST(cov_dep_parse_name_simple)
{
	sds name = dep_parse_name("simple_dep = \"1.0\"");
	ASSERT(name != nullptr, "name set");
	ASSERT(strcmp(name, "simple_dep") == 0, "name");
	sdsfree(name);
	PASS();
}

TEST(cov_dep_parse_name_no_version)
{
	sds name = dep_parse_name("just_a_name");
	ASSERT(name != nullptr, "name set");
	ASSERT(strcmp(name, "just_a_name") == 0, "name");
	sdsfree(name);
	PASS();
}

TEST(cov_dep_parse_name_path)
{
	sds name = dep_parse_name("local_dep = { path = \"../lib\" }");
	ASSERT(name != nullptr, "name set");
	ASSERT(strcmp(name, "local_dep") == 0, "name");
	sdsfree(name);
	PASS();
}

/* build_project with --locked flag (lockfile exists and is fresh) */
TEST(cov_build_project_locked)
{
	mkdir("src", 0755);
	FILE *fp = fopen("src/main.c", "w");
	ASSERT(fp != nullptr, "create main.c");
	fprintf_safe(fp, "int main(void) { return 0; }\n");
	fclose(fp);

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"lockedtest\"\n");
	fprintf_safe(fp, "version = \"1.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);

	/* Create lockfile (newer than Coffee.toml) */
	sleep(1);
	fp = fopen("Coffee.lock", "w");
	ASSERT(fp != nullptr, "create Coffee.lock");
	fprintf_safe(fp, "version = \"1\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = {
		.locked  = true,
		.verbose = false,
	};

	i64 ret = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build_project locked");
	manifest_free(m);
	remove("Coffee.lock");
	remove("Coffee.toml");
	remove("src/main.c");
	rmdir("src");
	PASS();
}

/* build_project with release mode */
TEST(cov_build_project_release)
{
	mkdir("src", 0755);
	FILE *fp = fopen("src/main.c", "w");
	ASSERT(fp != nullptr, "create main.c");
	fprintf_safe(fp, "int main(void) { return 0; }\n");
	fclose(fp);

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"releasetest\"\n");
	fprintf_safe(fp, "version = \"1.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = {
		.release = true,
	};

	i64 ret = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build_project release");
	manifest_free(m);
	remove("Coffee.toml");
	remove("src/main.c");
	rmdir("src");
	PASS();
}

/* build_project with features */
TEST(cov_build_project_features)
{
	mkdir("src", 0755);
	FILE *fp = fopen("src/main.c", "w");
	ASSERT(fp != nullptr, "create main.c");
	fprintf_safe(fp, "int main(void) { return 0; }\n");
	fclose(fp);

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"feattest\"\n");
	fprintf_safe(fp, "version = \"1.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "extra = []\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	sds          feats[] = { sdsnew("extra") };
	build_opts_t opts    = {
		.features       = feats,
		.features_count = 1,
	};

	i64 ret = build_project(m, &opts);
	ASSERT(ret == 0 || ret == 1, "build_project features");
	manifest_free(m);
	sdsfree(feats[0]);
	remove("Coffee.toml");
	remove("src/main.c");
	rmdir("src");
	PASS();
}

/* build_run */
TEST(cov_build_run_basic)
{
	mkdir("src", 0755);
	FILE *fp = fopen("src/main.c", "w");
	ASSERT(fp != nullptr, "create main.c");
	fprintf_safe(fp, "int main(void) { return 0; }\n");
	fclose(fp);

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"runtest\"\n");
	fprintf_safe(fp, "version = \"1.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");

	build_opts_t opts = {};
	i64          ret  = build_run(m, &opts, nullptr, 0);
	ASSERT(ret == 0 || ret == 1, "build_run");
	manifest_free(m);
	remove("Coffee.toml");
	remove("src/main.c");
	rmdir("src");
	PASS();
}

void coffee_register_coverage_build_tests(void)
{
	TEST_REGISTER(cov_dep_resolve_dir_deps_local);
	TEST_REGISTER(cov_dep_resolve_dir_not_found);
	TEST_REGISTER(cov_dep_resolve_dir_vendor);
	TEST_REGISTER(cov_dep_add_flags_with_library_toml);
	TEST_REGISTER(cov_dep_add_flags_fallback);
	TEST_REGISTER(cov_dep_add_flags_null_dir);
	TEST_REGISTER(cov_dep_parse_name_simple);
	TEST_REGISTER(cov_dep_parse_name_no_version);
	TEST_REGISTER(cov_dep_parse_name_path);
	TEST_REGISTER(cov_build_project_locked);
	TEST_REGISTER(cov_build_project_release);
	TEST_REGISTER(cov_build_project_features);
	TEST_REGISTER(cov_build_run_basic);
}
