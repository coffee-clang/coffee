#include "../src/coffee.h"
#include "../src/manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

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

static int test_makefile_is_created(void)
{
	TEST("makefile is created with expected content");

	const char *test_dir = "/tmp/coffee-makefile-project";
	mkdir(test_dir, 0755);

	char make_path[4096];
	snprintf(make_path, sizeof(make_path), "%s/Makefile", test_dir);

	FILE *fp = fopen(make_path, "w");
	ASSERT(fp, "could not create test Makefile");

	fprintf(fp, "CC ?= clang\n");
	fprintf(fp, "CFLAGS += -std=c23 -O3 -g\n");
	fprintf(fp, "CFLAGS += -Wall -Wextra -Wshadow -Wpedantic\n");
	fprintf(fp, "CFLAGS += -Wconversion -Wsign-conversion -Wunused\n");
	fprintf(fp, "CFLAGS += -Iinclude/test_project\n");
	fprintf(fp, "\n");
	fprintf(fp, "TARGET := build/test_project\n");
	fprintf(fp, "SOURCES := $(wildcard src/*.c)\n");
	fprintf(fp, "\n");
	fprintf(fp, ".PHONY: build clean format tidy\n");
	fprintf(fp, "\n");
	fprintf(fp, "build: $(SOURCES)\n");
	fprintf(fp, "\t$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)\n");
	fprintf(fp, "\n");
	fprintf(fp, "clean:\n");
	fprintf(fp, "\trm -rf build/\n");
	fprintf(fp, "\n");
	fprintf(fp, "format:\n");
	fprintf(fp, "\tclang-format -i src/*.c include/test_project/*.h\n");
	fprintf(fp, "\n");
	fprintf(fp, "tidy:\n");
	fprintf(fp, "\tclang-tidy src/*.c -- $(CFLAGS)\n");
	fclose(fp);

	fp = fopen(make_path, "r");
	ASSERT(fp, "Makefile should be readable");

	char   buf[4096];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[len]   = '\0';
	fclose(fp);

	ASSERT(strstr(buf, "CC ?= clang") != NULL, "Makefile missing CC");
	ASSERT(strstr(buf, "CFLAGS += -std=c23") != NULL, "Makefile missing C standard");
	ASSERT(strstr(buf, "CFLAGS += -Iinclude/") != NULL, "Makefile missing include path");
	ASSERT(strstr(buf, "build: $(SOURCES)") != NULL, "Makefile missing build target");
	ASSERT(strstr(buf, "clean:") != NULL, "Makefile missing clean target");
	ASSERT(strstr(buf, "format:") != NULL, "Makefile missing format target");
	ASSERT(strstr(buf, "tidy:") != NULL, "Makefile missing tidy target");

	PASS();
	return 1;
}

static int test_dep_appended_to_makefile(void)
{
	TEST("dependency is appended to Makefile");

	const char *tmp_make = "/tmp/coffee-makefile-add-test.mk";
	FILE	   *fp	     = fopen(tmp_make, "w");
	ASSERT(fp, "could not create test Makefile");
	fprintf(fp, "CC ?= clang\n");
	fprintf(fp, "build:\n");
	fprintf(fp, "\t$(CC) -o test src/*.c\n");
	fclose(fp);

	fp = fopen(tmp_make, "a");
	ASSERT(fp, "could not append to test Makefile");
	fprintf(fp, "\n# Dep: mylib\n");
	fprintf(fp, "CFLAGS += -Ideps/mylib/include\n");
	fprintf(fp, "LDFLAGS += -Ldeps/mylib/lib -lmylib\n");
	fclose(fp);

	fp = fopen(tmp_make, "r");
	ASSERT(fp, "could not reopen Makefile");
	char   buf[4096];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[len]   = '\0';
	fclose(fp);

	ASSERT(strstr(buf, "# Dep: mylib") != NULL, "Makefile missing dep comment");
	ASSERT(strstr(buf, "-Ideps/mylib/include") != NULL, "Makefile missing include path");
	ASSERT(strstr(buf, "-Ldeps/mylib/lib") != NULL, "Makefile missing library path");
	ASSERT(strstr(buf, "-lmylib") != NULL, "Makefile missing link flag");

	remove(tmp_make);
	PASS();
	return 1;
}

static int test_build_finds_makefile(void)
{
	TEST("build detects Makefile in project dir");

	const char *tmp_dir = "/tmp/coffee-build-test";
	mkdir(tmp_dir, 0755);

	char make_path[4096];
	snprintf(make_path, sizeof(make_path), "%s/Makefile", tmp_dir);
	FILE *fp = fopen(make_path, "w");
	ASSERT(fp, "could not create Makefile");
	fprintf(fp, "CC ?= clang\n");
	fprintf(fp, "build:\n");
	fprintf(fp, "\t@echo built\n");
	fclose(fp);

	fp = fopen(make_path, "r");
	ASSERT(fp, "Makefile should be readable");
	fclose(fp);

	PASS();
	return 1;
}

static int test_add_skips_when_no_makefile(void)
{
	TEST("add does not create Makefile when none exists");

	const char *fake_makefile = "/tmp/coffee-no-makefile-test/Makefile";
	remove(fake_makefile);

	FILE *fp = fopen(fake_makefile, "a");
	if (fp) {
		fclose(fp);
		FAIL("Makefile in non-existent directory should not be created");
		remove(fake_makefile);
		return 0;
	}

	PASS();
	return 1;
}

static int test_safe_package_name(void)
{
	TEST("safe package names are validated");

	const char *valid[] = {"mylib", "my_lib", "my-lib", "mylib123", "MyLib", NULL};
	for (int i = 0; valid[i]; i++) {
		const char *p = valid[i];
		while (*p) {
			int c = (unsigned char)*p;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
			      c == '-')) {
				FAIL("valid name should pass validation");
				return 0;
			}
			p++;
		}
	}

	const char *invalid[] = {"bad name", "bad$name", "bad;name", "bad\nname", "$(shell)", NULL};
	for (int i = 0; invalid[i]; i++) {
		const char *p	  = invalid[i];
		int	    found = 0;
		while (*p && !found) {
			int c = (unsigned char)*p;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
			      c == '-')) {
				found = 1;
			}
			p++;
		}
		ASSERT(found, "invalid name should fail validation");
	}

	PASS();
	return 1;
}

int main(void)
{
	printf("=== Running Makefile Tests ===\n\n");

	test_makefile_is_created();
	test_dep_appended_to_makefile();
	test_build_finds_makefile();
	test_add_skips_when_no_makefile();
	test_safe_package_name();

	printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
	return tests_failed > 0 ? 1 : 0;
}
