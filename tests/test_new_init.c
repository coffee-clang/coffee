/*
 * Integration tests for coffee init and coffee new.
 *
 * Verifies that generated source files are compilable with clang -std=c23.
 */

#include "../src/coffee.h"
#include "../src/manifest.h"
#include "../src/project.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_new_init_tests(void);

static char saved_cwd[4096];

static void pushd(const char *dir)
{
	assert(getcwd(saved_cwd, sizeof(saved_cwd)) != nullptr);
	assert(chdir(dir) == 0);
}

static void popd(void)
{
	chdir(saved_cwd);
}

static int file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

/* ── init ─────────────────────────────────────────────────────────────── */

TEST(init_generates_compilable_main)
{
	char tmpdir[] = "/tmp/coffee-init-test-XXXXXX";
	assert(mkdtemp(tmpdir) != nullptr);
	pushd(tmpdir);

	options opt = { .inputs_num = 0 };
	i64     ret = handle_init(&opt);
	ASSERT(ret == 0, "init should succeed");

	/* Verify key files exist */
	ASSERT(file_exists("src/main.c"), "src/main.c should exist");
	ASSERT(file_exists("Makefile"), "Makefile should exist");
	ASSERT(file_exists("Coffee.toml"), "Coffee.toml should exist");
	ASSERT(file_exists(".gitignore"), ".gitignore should exist");
	ASSERT(file_exists("LICENSE"), "LICENSE should exist");
	ASSERT(file_exists("README.md"), "README.md should exist");
	ASSERT(file_exists("docs/index.md"), "docs/index.md should exist");

	/* Try to compile the generated source */
	int rc = system("clang -std=c23 -fsyntax-only src/main.c 2>/dev/null");
	ASSERT(rc == 0, "generated main.c should compile with -std=c23");

	popd();
	system("rm -rf /tmp/coffee-init-test-*");
	PASS();
}

TEST(init_has_bin_section_when_main_present)
{
	char tmpdir[] = "/tmp/coffee-init-bin-XXXXXX";
	assert(mkdtemp(tmpdir) != nullptr);
	pushd(tmpdir);

	options opt = { .inputs_num = 0 };
	i64     ret = handle_init(&opt);
	ASSERT(ret == 0, "init should succeed");

	/* Coffee.toml should contain [[bin]] since main() is present */
	FILE *fp = fopen("Coffee.toml", "r");
	ASSERT(fp != nullptr, "Coffee.toml should be readable");
	char buf[4096];
	bool has_bin = false;
	while (fgets(buf, sizeof(buf), fp) != nullptr) {
		if (strstr(buf, "[[bin]]") != nullptr) {
			has_bin = true;
			break;
		}
	}
	fclose(fp);
	ASSERT(has_bin, "Coffee.toml should contain [[bin]] when main() is present");

	popd();
	system("rm -rf /tmp/coffee-init-bin-*");
	PASS();
}

/* ── new ──────────────────────────────────────────────────────────────── */

TEST(new_generates_compilable_main)
{
	char tmpdir[] = "/tmp/coffee-new-test-XXXXXX";
	assert(mkdtemp(tmpdir) != nullptr);
	pushd(tmpdir);

	sds proj_path = sdscatprintf(sdsempty(), "%s/myproject", tmpdir);

	options opt = { .inputs = (char *[]){ "new", proj_path }, .inputs_num = 2 };
	i64     ret = handle_new(&opt);
	ASSERT(ret == 0, "new should succeed");

	/* Verify key files exist */
	sds main_c = sdscatprintf(sdsempty(), "%s/src/main.c", proj_path);
	sds mf     = sdscatprintf(sdsempty(), "%s/Makefile", proj_path);
	sds toml   = sdscatprintf(sdsempty(), "%s/Coffee.toml", proj_path);
	ASSERT(file_exists(main_c), "src/main.c should exist");
	ASSERT(file_exists(mf), "Makefile should exist");
	ASSERT(file_exists(toml), "Coffee.toml should exist");

	/* Try to compile the generated source */
	sds cmd = sdscatprintf(sdsempty(), "clang -std=c23 -fsyntax-only %s 2>/dev/null", main_c);
	int rc  = system(cmd);
	sdsfree(cmd);
	ASSERT(rc == 0, "generated main.c should compile with -std=c23");

	sdsfree(main_c);
	sdsfree(mf);
	sdsfree(toml);
	sdsfree(proj_path);

	popd();
	system("rm -rf /tmp/coffee-new-test-*");
	PASS();
}

TEST(new_lib_generates_compilable_lib)
{
	char tmpdir[] = "/tmp/coffee-new-lib-XXXXXX";
	assert(mkdtemp(tmpdir) != nullptr);
	pushd(tmpdir);

	sds proj_path = sdscatprintf(sdsempty(), "%s/mylib", tmpdir);

	options opt = { .inputs = (char *[]){ "new", proj_path }, .inputs_num = 2, .lib = true };
	i64     ret = handle_new(&opt);
	ASSERT(ret == 0, "new --lib should succeed");

	/* Verify key files exist */
	sds lib_c = sdscatprintf(sdsempty(), "%s/src/lib.c", proj_path);
	sds mf    = sdscatprintf(sdsempty(), "%s/Makefile", proj_path);
	ASSERT(file_exists(lib_c), "src/lib.c should exist");
	ASSERT(file_exists(mf), "Makefile should exist");

	/* Try to compile the generated source using -I to find the header */
	sds cmd = sdscatprintf(sdsempty(), "clang -std=c23 -fsyntax-only -I%s/include %s 2>/dev/null", proj_path, lib_c);
	int rc  = system(cmd);
	sdsfree(cmd);
	ASSERT(rc == 0, "generated lib.c should compile with -std=c23");

	sdsfree(lib_c);
	sdsfree(mf);
	sdsfree(proj_path);

	popd();
	system("rm -rf /tmp/coffee-new-lib-*");
	PASS();
}

TEST(new_lib_template_uses_standard_c_types)
{
	char tmpdir[] = "/tmp/coffee-new-lib-types-XXXXXX";
	assert(mkdtemp(tmpdir) != nullptr);
	pushd(tmpdir);

	sds proj_path = sdscatprintf(sdsempty(), "%s/mylib2", tmpdir);

	options opt = { .inputs = (char *[]){ "new", proj_path }, .inputs_num = 2, .lib = true };
	i64     ret = handle_new(&opt);
	ASSERT(ret == 0, "new --lib should succeed");

	/* Read generated lib.c and verify it uses int not i64 */
	sds   lib_c_path = sdscatprintf(sdsempty(), "%s/src/lib.c", proj_path);
	FILE *fp         = fopen(lib_c_path, "r");
	ASSERT(fp != nullptr, "should be able to open lib.c");
	char   buf[4096] = { 0 };
	size_t total     = 0;
	while (fgets(buf + total, sizeof(buf) - total - 1, fp) != nullptr) {
		total += strlen(buf + total);
	}
	fclose(fp);

	ASSERT(strstr(buf, "int add(int a, int b)") != nullptr,
	       "lib.c template should use 'int add(int a, int b)' not i64");
	ASSERT(strstr(buf, "i64") == nullptr, "lib.c template should not contain 'i64'");

	sdsfree(lib_c_path);
	sdsfree(proj_path);

	popd();
	system("rm -rf /tmp/coffee-new-lib-types-*");
	PASS();
}

TEST(new_binary_template_uses_standard_c_types)
{
	char tmpdir[] = "/tmp/coffee-new-bin-types-XXXXXX";
	assert(mkdtemp(tmpdir) != nullptr);
	pushd(tmpdir);

	sds proj_path = sdscatprintf(sdsempty(), "%s/mybin", tmpdir);

	options opt = { .inputs = (char *[]){ "new", proj_path }, .inputs_num = 2 };
	i64     ret = handle_new(&opt);
	ASSERT(ret == 0, "new should succeed");

	/* Read generated main.c and verify it uses standard types */
	sds   main_c_path = sdscatprintf(sdsempty(), "%s/src/main.c", proj_path);
	FILE *fp          = fopen(main_c_path, "r");
	ASSERT(fp != nullptr, "should be able to open main.c");
	char   buf[4096] = { 0 };
	size_t total     = 0;
	while (fgets(buf + total, sizeof(buf) - total - 1, fp) != nullptr) {
		total += strlen(buf + total);
	}
	fclose(fp);

	ASSERT(strstr(buf, "int main(int argc, char **argv)") != nullptr,
	       "main.c template should use 'int main(int argc, char **argv)'");
	ASSERT(strstr(buf, "printf(\"Hello, world!") != nullptr, "main.c template should use 'printf' not printf_safe");
	ASSERT(strstr(buf, "printf_safe") == nullptr, "main.c template should not contain 'printf_safe'");
	ASSERT(strstr(buf, "i64") == nullptr, "main.c template should not contain 'i64'");

	sdsfree(main_c_path);
	sdsfree(proj_path);

	popd();
	system("rm -rf /tmp/coffee-new-bin-types-*");
	PASS();
}

void coffee_register_new_init_tests(void)
{
	TEST_REGISTER(init_generates_compilable_main);
	TEST_REGISTER(init_has_bin_section_when_main_present);
	TEST_REGISTER(new_generates_compilable_main);
	TEST_REGISTER(new_lib_generates_compilable_lib);
	TEST_REGISTER(new_lib_template_uses_standard_c_types);
	TEST_REGISTER(new_binary_template_uses_standard_c_types);
}
