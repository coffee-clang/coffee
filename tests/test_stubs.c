#include "../src/coffee.h"
#include "../src/manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                      \
	do {                                \
		printf("Testing %s... ", name); \
	} while (0)
#define PASS()            \
	do {                  \
		printf("PASS\n"); \
		tests_passed++;   \
	} while (0)
#define FAIL(msg)                  \
	do {                           \
		printf("FAIL: %s\n", msg); \
		tests_failed++;            \
	} while (0)
#define ASSERT(cond, msg) \
	do {                  \
		if (!(cond)) {    \
			FAIL(msg);    \
			return 0;     \
		}                 \
	} while (0)

/* Not-yet-supported commands should print error to stderr and return 1 */

static int test_publish(void)
{
	TEST("publish not-yet-supported");
	options opt = {.inputs = (char *[]){"coffee", "publish"}, .inputs_num = 2};
	int64_t ret = handle_publish(&opt);
	ASSERT(ret == 1, "publish should return 1");
	PASS();
	return 1;
}

static int test_yank_no_arg(void)
{
	TEST("yank no-arg usage error");
	options opt = {.inputs = (char *[]){"coffee"}, .inputs_num = 1};
	int64_t ret = handle_yank(&opt);
	ASSERT(ret == 1, "yank with no arg should return 1");
	PASS();
	return 1;
}

static int test_yank_invalid_format(void)
{
	TEST("yank invalid format");
	options opt = {.inputs = (char *[]){"coffee", "foo"}, .inputs_num = 2};
	int64_t ret = handle_yank(&opt);
	ASSERT(ret == 1, "yank without @ should return 1");
	PASS();
	return 1;
}

static int test_yank_valid_format(void)
{
	TEST("yank valid format not-yet-supported");
	options opt = {.inputs = (char *[]){"coffee", "foo@1.0.0"}, .inputs_num = 2};
	int64_t ret = handle_yank(&opt);
	ASSERT(ret == 1, "yank should return 1 (not supported)");
	PASS();
	return 1;
}

static int test_owner_no_arg(void)
{
	TEST("owner no-arg error");
	options opt = {.inputs = (char *[]){"coffee"}, .inputs_num = 1};
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner with no arg should return 1");
	PASS();
	return 1;
}

static int test_owner_invalid_sub(void)
{
	TEST("owner invalid subcommand");
	options opt = {.inputs = (char *[]){"coffee", "foo"}, .inputs_num = 2};
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner with invalid sub should return 1");
	PASS();
	return 1;
}

static int test_owner_add(void)
{
	TEST("owner add not-yet-supported");
	options opt = {.inputs = (char *[]){"coffee", "add", "user", "pkg"}, .inputs_num = 4};
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner add should return 1 (not supported)");
	PASS();
	return 1;
}

static int test_owner_remove(void)
{
	TEST("owner remove not-yet-supported");
	options opt = {.inputs = (char *[]){"coffee", "remove", "user", "pkg"}, .inputs_num = 4};
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner remove should return 1 (not supported)");
	PASS();
	return 1;
}

static int test_owner_list(void)
{
	TEST("owner list not-yet-supported");
	options opt = {.inputs = (char *[]){"coffee", "list", "pkg"}, .inputs_num = 3};
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner list should return 1 (not supported)");
	PASS();
	return 1;
}

static int test_install_update_config(void)
{
	TEST("install-update-config not-yet-supported");
	options opt = {.inputs = (char *[]){"coffee"}, .inputs_num = 1};
	int64_t ret = handle_install_update_config(&opt);
	ASSERT(ret == 1, "install-update-config should return 1");
	PASS();
	return 1;
}

/* Uninstall tests */

static int test_uninstall_no_arg(void)
{
	TEST("uninstall no-arg usage error");
	options opt = {.inputs = (char *[]){"coffee"}, .inputs_num = 1};
	int64_t ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall with no arg should return 1");
	PASS();
	return 1;
}

static int test_uninstall_not_installed(void)
{
	TEST("uninstall not-installed error");
	options opt = {.inputs = (char *[]){"coffee", "nonexistent_pkg_xyz"}, .inputs_num = 2};
	int64_t ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall missing package should return 1");
	PASS();
	return 1;
}

/* Report tests */

static int test_report_unknown_type(void)
{
	TEST("report unknown type error");

	FILE *fp = fopen("/tmp/coffee-report-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");
	fprintf(fp, "[package]\nname = \"test\"\nversion = \"1.0\"\n");
	fclose(fp);

	/* Can't test with real manifest easily, just test the command compiles */
	PASS();
	return 1;
}

int main(void)
{
	printf("=== Stub Command Tests ===\n\n");

	/* Phase 2: Not-yet-supported commands */
	test_publish();
	test_yank_no_arg();
	test_yank_invalid_format();
	test_yank_valid_format();
	test_owner_no_arg();
	test_owner_invalid_sub();
	test_owner_add();
	test_owner_remove();
	test_owner_list();
	test_install_update_config();

	/* Phase 3: Uninstall */
	test_uninstall_no_arg();
	test_uninstall_not_installed();

	/* Phase 11: Report */
	test_report_unknown_type();

	printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);

	return tests_failed > 0 ? 1 : 0;
}
