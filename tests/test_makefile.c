#include "../src/coffee.h"
#include "../src/manifest.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

TEST(makefile_is_created)
{
	const char *test_dir = "/tmp/coffee-makefile-project";
	mkdir(test_dir, 0755);

	sds make_path = sdscatprintf(sdsempty(), "%s/Makefile", test_dir);

	FILE *fp = fopen(make_path, "w");
	ASSERT(fp, "could not create test Makefile");

	fprintf_safe(fp, "CC ?= clang\n");
	fprintf_safe(fp, "CFLAGS += -std=c23 -O3 -g\n");
	fprintf_safe(fp, "CFLAGS += -Wall -Wextra -Wshadow -Wpedantic\n");
	fprintf_safe(fp, "CFLAGS += -Wconversion -Wsign-conversion -Wunused\n");
	fprintf_safe(fp, "CFLAGS += -Iinclude/test_project\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "TARGET := build/test_project\n");
	fprintf_safe(fp, "SOURCES := $(wildcard src/*.c)\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, ".PHONY: build clean format tidy\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "build: $(SOURCES)\n");
	fprintf_safe(fp, "\t$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "clean:\n");
	fprintf_safe(fp, "\trm -rf build/\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "format:\n");
	fprintf_safe(fp, "\tclang-format -i src/*.c include/test_project/*.h\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "tidy:\n");
	fprintf_safe(fp, "\tclang-tidy src/*.c -- $(CFLAGS)\n");
	fclose(fp);

	fp = fopen(make_path, "r");
	ASSERT(fp, "Makefile should be readable");

	sds    buf = sdsempty();
	char   chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		buf = sdscatlen(buf, chunk, n);
	}
	fclose(fp);

	ASSERT(strstr(buf, "CC ?= clang") != nullptr, "Makefile missing CC");
	ASSERT(strstr(buf, "CFLAGS += -std=c23") != nullptr, "Makefile missing C standard");
	ASSERT(strstr(buf, "CFLAGS += -Iinclude/") != nullptr, "Makefile missing include path");
	ASSERT(strstr(buf, "build: $(SOURCES)") != nullptr, "Makefile missing build target");
	ASSERT(strstr(buf, "clean:") != nullptr, "Makefile missing clean target");
	ASSERT(strstr(buf, "format:") != nullptr, "Makefile missing format target");
	ASSERT(strstr(buf, "tidy:") != nullptr, "Makefile missing tidy target");

	sdsfree(buf);
	sdsfree(make_path);
	PASS();
}

TEST(dep_appended_to_makefile)
{
	const char *tmp_make = "/tmp/coffee-makefile-add-test.mk";
	FILE       *fp       = fopen(tmp_make, "w");
	ASSERT(fp, "could not create test Makefile");
	fprintf_safe(fp, "CC ?= clang\n");
	fprintf_safe(fp, "build:\n");
	fprintf_safe(fp, "\t$(CC) -o test src/*.c\n");
	fclose(fp);

	fp = fopen(tmp_make, "a");
	ASSERT(fp, "could not append to test Makefile");
	fprintf_safe(fp, "\n# Dep: mylib\n");
	fprintf_safe(fp, "CFLAGS += -Ideps/mylib/include\n");
	fprintf_safe(fp, "LDFLAGS += -Ldeps/mylib/lib -lmylib\n");
	fclose(fp);

	fp = fopen(tmp_make, "r");
	ASSERT(fp, "could not reopen Makefile");
	sds    buf = sdsempty();
	char   chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		buf = sdscatlen(buf, chunk, n);
	}
	fclose(fp);

	ASSERT(strstr(buf, "# Dep: mylib") != nullptr, "Makefile missing dep comment");
	ASSERT(strstr(buf, "-Ideps/mylib/include") != nullptr, "Makefile missing include path");
	ASSERT(strstr(buf, "-Ldeps/mylib/lib") != nullptr, "Makefile missing library path");
	ASSERT(strstr(buf, "-lmylib") != nullptr, "Makefile missing link flag");

	sdsfree(buf);
	remove(tmp_make);
	PASS();
}

TEST(build_finds_makefile)
{
	const char *tmp_dir = "/tmp/coffee-build-test";
	mkdir(tmp_dir, 0755);

	sds   make_path = sdscatprintf(sdsempty(), "%s/Makefile", tmp_dir);
	FILE *fp        = fopen(make_path, "w");
	ASSERT(fp, "could not create Makefile");
	fprintf_safe(fp, "CC ?= clang\n");
	fprintf_safe(fp, "build:\n");
	fprintf_safe(fp, "\t@echo built\n");
	fclose(fp);

	fp = fopen(make_path, "r");
	ASSERT(fp, "Makefile should be readable");
	fclose(fp);

	sdsfree(make_path);
	PASS();
}

TEST(add_skips_when_no_makefile)
{
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
}

TEST(safe_package_name)
{
	const char *valid[] = { "mylib", "my_lib", "my-lib", "mylib123", "MyLib", nullptr };
	for (i64 i = 0; valid[i]; i++) {
		const char *p = valid[i];
		while (*p) {
			i64 c = (unsigned char)*p;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
				FAIL("valid name should pass validation");
				return 0;
			}
			p++;
		}
	}

	const char *invalid[] = { "bad name", "bad$name", "bad;name", "bad\nname", "$(shell)", nullptr };
	for (i64 i = 0; invalid[i]; i++) {
		const char *p     = invalid[i];
		i64         found = 0;
		while (*p && !found) {
			i64 c = (unsigned char)*p;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
				found = 1;
			}
			p++;
		}
		ASSERT(found, "invalid name should fail validation");
	}

	PASS();
}

void coffee_register_makefile_tests(void)
{
	TEST_REGISTER(makefile_is_created);
	TEST_REGISTER(dep_appended_to_makefile);
	TEST_REGISTER(build_finds_makefile);
	TEST_REGISTER(add_skips_when_no_makefile);
	TEST_REGISTER(safe_package_name);
}