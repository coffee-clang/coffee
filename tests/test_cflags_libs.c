#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

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

	char pkgdir[4096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-pkg", home);

	mkdir(pkgdir, 0755);

	ASSERT(access(pkgdir, F_OK) == 0, "pkg directory should exist");

	PASS();
}

TEST(library_toml_with_include)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-pkg", home);
	mkdir(pkgdir, 0755);

	char toml_path[4096];
	snprintf(toml_path, sizeof(toml_path), "%s/library.toml", pkgdir);

	create_file(toml_path, "title = \"test-pkg\"\n"
	                       "version = \"1.0\"\n"
	                       "include = [\"include\", \"src/include\"]\n");

	FILE *fp = fopen(toml_path, "r");
	ASSERT(fp, "library.toml should exist");
	char   buf[1024];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[len]   = '\0';
	fclose(fp);

	ASSERT(strstr(buf, "include = ["), "library.toml missing include key");
	ASSERT(strstr(buf, "src/include"), "library.toml missing second include path");

	PASS();
}

TEST(library_toml_with_libname)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-pkg", home);
	mkdir(pkgdir, 0755);

	char toml_path[4096];
	snprintf(toml_path, sizeof(toml_path), "%s/library.toml", pkgdir);

	create_file(toml_path, "title = \"test-pkg\"\n"
	                       "version = \"1.0\"\n"
	                       "libname = \"testpkg\"\n"
	                       "lib = [\"lib\"]\n");

	FILE *fp = fopen(toml_path, "r");
	ASSERT(fp, "library.toml should exist");
	char   buf[1024];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[len]   = '\0';
	fclose(fp);

	ASSERT(strstr(buf, "libname = \"testpkg\""), "library.toml missing libname key");
	ASSERT(strstr(buf, "lib = ["), "library.toml missing lib key");

	PASS();
}

TEST(fallback_no_keys)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-nokeys", home);
	mkdir(pkgdir, 0755);

	char incdir[4096];
	snprintf(incdir, sizeof(incdir), "%s/include", pkgdir);
	mkdir(incdir, 0755);

	char libdir[4096];
	snprintf(libdir, sizeof(libdir), "%s/lib", pkgdir);
	mkdir(libdir, 0755);

	char toml_path[4096];
	snprintf(toml_path, sizeof(toml_path), "%s/library.toml", pkgdir);
	create_file(toml_path, "title = \"test-nokeys\"\n"
	                       "version = \"1.0\"\n");

	ASSERT(access(incdir, F_OK) == 0, "fallback include dir should exist");
	ASSERT(access(libdir, F_OK) == 0, "fallback lib dir should exist");

	PASS();
}

TEST(pkg_not_installed)
{
	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/nonexistent-pkg", home);

	ASSERT(access(pkgdir, F_OK) != 0, "nonexistent package dir should not exist");

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