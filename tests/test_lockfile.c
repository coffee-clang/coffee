#include "../src/lockfile.h"
#include "../src/strings.h"
#include "safe.h"
#include "test_framework.h"

/* Forward declaration */
void coffee_register_lockfile_tests(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

TEST(lockfile_parse_simple)
{
	FILE *fp = fopen("/tmp/coffee-lockfile-test.toml", "w");
	ASSERT(fp != nullptr, "could not create test lockfile");
	fprintf_safe(fp, "version = 1\n\n");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"testpkg\"\n");
	fprintf_safe(fp, "version = \"0.1.0\"\n\n");
	fprintf_safe(fp, "[[dependencies]]\n");
	fprintf_safe(fp, "name = \"toml\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "path = \"deps/toml\"\n");
	fclose(fp);

	lockfile_t *lf = lockfile_parse("/tmp/coffee-lockfile-test.toml");
	ASSERT(lf != nullptr, "lockfile_parse returned nullptr");
	ASSERT(lf->version == 1, "version should be 1");
	ASSERT(lf->package_name != nullptr, "package_name is nullptr");
	ASSERT(strcmp(lf->package_name, "testpkg") == 0, "package_name mismatch");
	ASSERT(lf->package_version != nullptr, "package_version is nullptr");
	ASSERT(strcmp(lf->package_version, "0.1.0") == 0, "package_version mismatch");
	ASSERT(lf->deps_count == 1, "expected 1 dep");
	ASSERT(lf->deps[0].name != nullptr, "dep name is nullptr");
	ASSERT(strcmp(lf->deps[0].name, "toml") == 0, "dep name mismatch");
	ASSERT(lf->deps[0].version != nullptr, "dep version is nullptr");
	ASSERT(strcmp(lf->deps[0].version, "1.0.0") == 0, "dep version mismatch");
	ASSERT(lf->deps[0].path != nullptr, "dep path is nullptr");
	ASSERT(strcmp(lf->deps[0].path, "deps/toml") == 0, "dep path mismatch");

	lockfile_free(lf);
	remove("/tmp/coffee-lockfile-test.toml");
	PASS();
}

TEST(lockfile_parse_missing_file)
{
	lockfile_t *lf = lockfile_parse("/tmp/coffee-nonexistent-lockfile.toml");
	ASSERT(lf == nullptr, "should return nullptr for missing file");
	PASS();
}

TEST(lockfile_parse_multiple_deps)
{
	FILE *fp = fopen("/tmp/coffee-lockfile-multi.toml", "w");
	ASSERT(fp != nullptr, "could not create test lockfile");
	fprintf_safe(fp, "version = 1\n\n");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"mypkg\"\n");
	fprintf_safe(fp, "version = \"2.0.0\"\n\n");
	fprintf_safe(fp, "[[dependencies]]\n");
	fprintf_safe(fp, "name = \"toml\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "path = \"deps/toml\"\n\n");
	fprintf_safe(fp, "[[dependencies]]\n");
	fprintf_safe(fp, "name = \"sds\"\n");
	fprintf_safe(fp, "version = \"2.0.0\"\n");
	fprintf_safe(fp, "path = \"deps/sds\"\n");
	fclose(fp);

	lockfile_t *lf = lockfile_parse("/tmp/coffee-lockfile-multi.toml");
	ASSERT(lf != nullptr, "lockfile_parse returned nullptr");
	ASSERT(lf->deps_count == 2, "expected 2 deps");
	ASSERT(strcmp(lf->deps[0].name, "toml") == 0, "first dep name mismatch");
	ASSERT(strcmp(lf->deps[1].name, "sds") == 0, "second dep name mismatch");
	ASSERT(strcmp(lf->deps[1].path, "deps/sds") == 0, "second dep path mismatch");

	lockfile_free(lf);
	remove("/tmp/coffee-lockfile-multi.toml");
	PASS();
}

TEST(lockfile_find_dep)
{
	FILE *fp = fopen("/tmp/coffee-lockfile-find.toml", "w");
	ASSERT(fp != nullptr, "could not create test lockfile");
	fprintf_safe(fp, "version = 1\n\n");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"pkg\"\n");
	fprintf_safe(fp, "version = \"1.0\"\n\n");
	fprintf_safe(fp, "[[dependencies]]\n");
	fprintf_safe(fp, "name = \"toml\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "path = \"deps/toml\"\n\n");
	fprintf_safe(fp, "[[dependencies]]\n");
	fprintf_safe(fp, "name = \"sds\"\n");
	fprintf_safe(fp, "version = \"2.0.0\"\n");
	fprintf_safe(fp, "path = \"deps/sds\"\n");
	fclose(fp);

	lockfile_t *lf = lockfile_parse("/tmp/coffee-lockfile-find.toml");
	ASSERT(lf != nullptr, "lockfile_parse returned nullptr");

	lockfile_dep_t *dep = lockfile_find_dep(lf, "toml");
	ASSERT(dep != nullptr, "find_dep returned nullptr for 'toml'");
	ASSERT(strcmp(dep->version, "1.0.0") == 0, "toml version mismatch");

	dep = lockfile_find_dep(lf, "sds");
	ASSERT(dep != nullptr, "find_dep returned nullptr for 'sds'");
	ASSERT(strcmp(dep->path, "deps/sds") == 0, "sds path mismatch");

	dep = lockfile_find_dep(lf, "nonexistent");
	ASSERT(dep == nullptr, "find_dep should return nullptr for missing dep");

	lockfile_free(lf);
	remove("/tmp/coffee-lockfile-find.toml");
	PASS();
}

TEST(lockfile_write_and_reparse)
{
	lockfile_t *lf = safe_calloc(1, sizeof(lockfile_t));
	ASSERT(lf != nullptr, "calloc failed");

	lf->version         = 1;
	lf->package_name    = sdsnew("testwrite");
	lf->package_version = sdsnew("3.0.0");
	lf->deps_count      = 2;
	lf->deps            = safe_calloc(2, sizeof(lockfile_dep_t));
	ASSERT(lf->deps != nullptr, "calloc for deps failed");

	lf->deps[0].name    = sdsnew("dep-a");
	lf->deps[0].version = sdsnew("1.5.0");
	lf->deps[0].path    = sdsnew("deps/dep-a");

	lf->deps[1].name    = sdsnew("dep-b");
	lf->deps[1].version = sdsnew("2.0.0");
	lf->deps[1].path    = sdsnew("vendor/dep-b");

	i64 ret = lockfile_write("/tmp/coffee-lockfile-write-test.toml", lf);
	ASSERT(ret == 0, "lockfile_write returned nonzero");

	lockfile_free(lf);

	/* Re-parse and verify */
	lockfile_t *lf2 = lockfile_parse("/tmp/coffee-lockfile-write-test.toml");
	ASSERT(lf2 != nullptr, "re-parse returned nullptr");
	ASSERT(lf2->version == 1, "version mismatch");
	ASSERT(strcmp(lf2->package_name, "testwrite") == 0, "package_name mismatch");
	ASSERT(strcmp(lf2->package_version, "3.0.0") == 0, "package_version mismatch");
	ASSERT(lf2->deps_count == 2, "deps_count mismatch");
	ASSERT(strcmp(lf2->deps[0].name, "dep-a") == 0, "first dep name mismatch");
	ASSERT(strcmp(lf2->deps[0].version, "1.5.0") == 0, "first dep version mismatch");
	ASSERT(strcmp(lf2->deps[0].path, "deps/dep-a") == 0, "first dep path mismatch");
	ASSERT(strcmp(lf2->deps[1].name, "dep-b") == 0, "second dep name mismatch");
	ASSERT(strcmp(lf2->deps[1].version, "2.0.0") == 0, "second dep version mismatch");
	ASSERT(strcmp(lf2->deps[1].path, "vendor/dep-b") == 0, "second dep path mismatch");

	lockfile_free(lf2);
	remove("/tmp/coffee-lockfile-write-test.toml");
	PASS();
}

void coffee_register_lockfile_tests(void)
{
	TEST_REGISTER(lockfile_parse_simple);
	TEST_REGISTER(lockfile_parse_missing_file);
	TEST_REGISTER(lockfile_parse_multiple_deps);
	TEST_REGISTER(lockfile_find_dep);
	TEST_REGISTER(lockfile_write_and_reparse);
}
