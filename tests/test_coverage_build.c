/*
 * Coverage tests for build.c uncovered paths.
 *
 * Targets:
 *   - dep_resolve_dir with found dirs (deps/, vendor/, global)
 *   - dep_parse_name
 *   - dep_add_flags with library.toml, fallbacks, source glob
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
	ASSERT(strstr(flags, "-l\"testlib\"") != nullptr, "flags contain -l\"testlib\"");

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
	ASSERT(strstr(flags, "-l\"fallback\"") != nullptr, "flags contain -l\"fallback\"");

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
}
