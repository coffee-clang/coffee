#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_cflags_libs_tests(void);

static void create_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");
	if (fp) {
		fprintf_safe(fp, "%s", content);
		fclose(fp);
	}
}

TEST(pkg_dir_exists)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	sds coffee = sdscatprintf(sdsempty(), "%s/.coffee", home);
	mkdir(coffee, 0755);
	sdsfree(coffee);
	sds depsdir = sdscatprintf(sdsempty(), "%s/.coffee/deps", home);
	mkdir(depsdir, 0755);
	sdsfree(depsdir);

	sds pkgdir = sdscatprintf(sdsempty(), "%s/.coffee/deps/test-pkg", home);

	mkdir(pkgdir, 0755);

	ASSERT(access(pkgdir, F_OK) == 0, "pkg directory should exist");
	sdsfree(pkgdir);

	PASS();
}

TEST(library_toml_with_include)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	sds coffee = sdscatprintf(sdsempty(), "%s/.coffee", home);
	mkdir(coffee, 0755);
	sdsfree(coffee);
	sds depsdir = sdscatprintf(sdsempty(), "%s/.coffee/deps", home);
	mkdir(depsdir, 0755);
	sdsfree(depsdir);

	sds pkgdir = sdscatprintf(sdsempty(), "%s/.coffee/deps/test-pkg", home);
	mkdir(pkgdir, 0755);

	sds toml_path = sdscatprintf(sdsempty(), "%s/library.toml", pkgdir);

	create_file(toml_path, "title = \"test-pkg\"\n"
	                       "version = \"1.0\"\n"
	                       "include = [\"include\", \"src/include\"]\n");

	FILE *fp = fopen(toml_path, "r");
	ASSERT(fp, "library.toml should exist");
	sds    buf = sdsempty();
	char   chunk[1024];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		buf = sdscatlen(buf, chunk, n);
	}
	fclose(fp);

	sdsfree(toml_path);
	sdsfree(pkgdir);

	ASSERT(strstr(buf, "include = ["), "library.toml missing include key");
	ASSERT(strstr(buf, "src/include"), "library.toml missing second include path");

	sdsfree(buf);
	PASS();
}

TEST(library_toml_with_libname)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	sds coffee = sdscatprintf(sdsempty(), "%s/.coffee", home);
	mkdir(coffee, 0755);
	sdsfree(coffee);
	sds depsdir = sdscatprintf(sdsempty(), "%s/.coffee/deps", home);
	mkdir(depsdir, 0755);
	sdsfree(depsdir);

	sds pkgdir = sdscatprintf(sdsempty(), "%s/.coffee/deps/test-pkg", home);
	mkdir(pkgdir, 0755);

	sds toml_path = sdscatprintf(sdsempty(), "%s/library.toml", pkgdir);

	create_file(toml_path, "title = \"test-pkg\"\n"
	                       "version = \"1.0\"\n"
	                       "libname = \"testpkg\"\n"
	                       "lib = [\"lib\"]\n");

	FILE *fp = fopen(toml_path, "r");
	ASSERT(fp, "library.toml should exist");
	sds    buf = sdsempty();
	char   chunk[1024];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		buf = sdscatlen(buf, chunk, n);
	}
	fclose(fp);

	sdsfree(toml_path);
	sdsfree(pkgdir);

	ASSERT(strstr(buf, "libname = \"testpkg\""), "library.toml missing libname key");
	ASSERT(strstr(buf, "lib = ["), "library.toml missing lib key");

	sdsfree(buf);
	PASS();
}

TEST(fallback_no_keys)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	sds coffee = sdscatprintf(sdsempty(), "%s/.coffee", home);
	mkdir(coffee, 0755);
	sdsfree(coffee);
	sds depsdir = sdscatprintf(sdsempty(), "%s/.coffee/deps", home);
	mkdir(depsdir, 0755);
	sdsfree(depsdir);

	sds pkgdir = sdscatprintf(sdsempty(), "%s/.coffee/deps/test-nokeys", home);
	mkdir(pkgdir, 0755);

	sds incdir = sdscatprintf(sdsempty(), "%s/include", pkgdir);
	mkdir(incdir, 0755);

	sds libdir = sdscatprintf(sdsempty(), "%s/lib", pkgdir);
	mkdir(libdir, 0755);

	sds toml_path = sdscatprintf(sdsempty(), "%s/library.toml", pkgdir);
	create_file(toml_path, "title = \"test-nokeys\"\n"
	                       "version = \"1.0\"\n");

	ASSERT(access(incdir, F_OK) == 0, "fallback include dir should exist");
	ASSERT(access(libdir, F_OK) == 0, "fallback lib dir should exist");

	sdsfree(toml_path);
	sdsfree(incdir);
	sdsfree(libdir);
	sdsfree(pkgdir);

	PASS();
}

TEST(pkg_not_installed)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	sds pkgdir = sdscatprintf(sdsempty(), "%s/.coffee/deps/nonexistent-pkg", home);

	ASSERT(access(pkgdir, F_OK) != 0, "nonexistent package dir should not exist");
	sdsfree(pkgdir);

	PASS();
}

void coffee_register_cflags_libs_tests(void)
{
	TEST_REGISTER(pkg_dir_exists);
	TEST_REGISTER(library_toml_with_include);
	TEST_REGISTER(library_toml_with_libname);
	TEST_REGISTER(fallback_no_keys);
	TEST_REGISTER(pkg_not_installed);
}
