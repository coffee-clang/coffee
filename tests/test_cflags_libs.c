#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                              \
	do {                                    \
		printf("Testing %s... ", name); \
	} while (0)
#define PASS()                    \
	do {                      \
		printf("PASS\n"); \
		tests_passed++;   \
	} while (0)
#define FAIL(msg)                          \
	do {                               \
		printf("FAIL: %s\n", msg); \
		tests_failed++;            \
	} while (0)
#define ASSERT(cond, msg)          \
	do {                       \
		if (!(cond)) {     \
			FAIL(msg); \
			return 0;  \
		}                  \
	} while (0)

static void create_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");
	if (fp) {
		fprintf(fp, "%s", content);
		fclose(fp);
	}
}

static int test_pkg_dir_exists(void)
{
	TEST("pkg directory is resolved correctly");

	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4'096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-pkg", home);

	mkdir(pkgdir, 0755);

	ASSERT(access(pkgdir, F_OK) == 0, "pkg directory should exist");

	PASS();
	return 1;
}

static int test_library_toml_with_include(void)
{
	TEST("library.toml include key is read correctly");

	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4'096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-pkg", home);
	mkdir(pkgdir, 0755);

	char toml_path[4'096];
	snprintf(toml_path, sizeof(toml_path), "%s/library.toml", pkgdir);

	create_file(toml_path,
		"title = \"test-pkg\"\n"
		"version = \"1.0\"\n"
		"include = [\"include\", \"src/include\"]\n");

	FILE *fp = fopen(toml_path, "r");
	ASSERT(fp, "library.toml should exist");
	char buf[1024];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[len] = '\0';
	fclose(fp);

	ASSERT(strstr(buf, "include = ["), "library.toml missing include key");
	ASSERT(strstr(buf, "src/include"), "library.toml missing second include path");

	PASS();
	return 1;
}

static int test_library_toml_with_libname(void)
{
	TEST("library.toml libname key is read correctly");

	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4'096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-pkg", home);
	mkdir(pkgdir, 0755);

	char toml_path[4'096];
	snprintf(toml_path, sizeof(toml_path), "%s/library.toml", pkgdir);

	create_file(toml_path,
		"title = \"test-pkg\"\n"
		"version = \"1.0\"\n"
		"libname = \"testpkg\"\n"
		"lib = [\"lib\"]\n");

	FILE *fp = fopen(toml_path, "r");
	ASSERT(fp, "library.toml should exist");
	char buf[1024];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[len] = '\0';
	fclose(fp);

	ASSERT(strstr(buf, "libname = \"testpkg\""), "library.toml missing libname key");
	ASSERT(strstr(buf, "lib = ["), "library.toml missing lib key");

	PASS();
	return 1;
}

static int test_fallback_no_keys(void)
{
	TEST("fallback when library.toml has no include/lib keys");

	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4'096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/test-nokeys", home);
	mkdir(pkgdir, 0755);

	char incdir[4'096];
	snprintf(incdir, sizeof(incdir), "%s/include", pkgdir);
	mkdir(incdir, 0755);

	char libdir[4'096];
	snprintf(libdir, sizeof(libdir), "%s/lib", pkgdir);
	mkdir(libdir, 0755);

	char toml_path[4'096];
	snprintf(toml_path, sizeof(toml_path), "%s/library.toml", pkgdir);
	create_file(toml_path,
		"title = \"test-nokeys\"\n"
		"version = \"1.0\"\n");

	ASSERT(access(incdir, F_OK) == 0, "fallback include dir should exist");
	ASSERT(access(libdir, F_OK) == 0, "fallback lib dir should exist");

	PASS();
	return 1;
}

static int test_pkg_not_installed(void)
{
	TEST("uninstalled package is skipped");

	const char *home = getenv("HOME");
	ASSERT(home, "HOME not set");

	char pkgdir[4'096];
	snprintf(pkgdir, sizeof(pkgdir), "%s/.coffee/deps/nonexistent-pkg", home);

	ASSERT(access(pkgdir, F_OK) != 0, "nonexistent package dir should not exist");

	PASS();
	return 1;
}

int main(void)
{
	printf("=== Running cflags/libs Tests ===\n\n");

	test_pkg_dir_exists();
	test_library_toml_with_include();
	test_library_toml_with_libname();
	test_fallback_no_keys();
	test_pkg_not_installed();

	printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
	return tests_failed > 0 ? 1 : 0;
}
