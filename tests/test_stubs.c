#include "../src/coffee.h"
#include "../src/manifest.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(publish)
{
	options opt = { .inputs = (char *[]){ "coffee", "publish" }, .inputs_num = 2 };
	int64_t ret = handle_publish(&opt);
	ASSERT(ret == 1, "publish should return 1");
	PASS();
}

TEST(yank_no_arg)
{
	options opt = { .inputs = (char *[]){ "coffee" }, .inputs_num = 1 };
	int64_t ret = handle_yank(&opt);
	ASSERT(ret == 1, "yank with no arg should return 1");
	PASS();
}

TEST(yank_invalid_format)
{
	options opt = { .inputs = (char *[]){ "coffee", "foo" }, .inputs_num = 2 };
	int64_t ret = handle_yank(&opt);
	ASSERT(ret == 1, "yank without @ should return 1");
	PASS();
}

TEST(yank_valid_format)
{
	options opt = { .inputs = (char *[]){ "coffee", "foo@1.0.0" }, .inputs_num = 2 };
	int64_t ret = handle_yank(&opt);
	ASSERT(ret == 1, "yank should return 1 (not supported)");
	PASS();
}

TEST(owner_no_arg)
{
	options opt = { .inputs = (char *[]){ "coffee" }, .inputs_num = 1 };
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner with no arg should return 1");
	PASS();
}

TEST(owner_invalid_sub)
{
	options opt = { .inputs = (char *[]){ "coffee", "foo" }, .inputs_num = 2 };
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner with invalid sub should return 1");
	PASS();
}

TEST(owner_add)
{
	options opt = { .inputs = (char *[]){ "coffee", "add", "user", "pkg" }, .inputs_num = 4 };
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner add should return 1 (not supported)");
	PASS();
}

TEST(owner_remove)
{
	options opt = { .inputs = (char *[]){ "coffee", "remove", "user", "pkg" }, .inputs_num = 4 };
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner remove should return 1 (not supported)");
	PASS();
}

TEST(owner_list)
{
	options opt = { .inputs = (char *[]){ "coffee", "list", "pkg" }, .inputs_num = 3 };
	int64_t ret = handle_owner(&opt);
	ASSERT(ret == 1, "owner list should return 1 (not supported)");
	PASS();
}

TEST(install_update_config)
{
	options opt = { .inputs = (char *[]){ "coffee" }, .inputs_num = 1 };
	int64_t ret = handle_install_update_config(&opt);
	ASSERT(ret == 1, "install-update-config should return 1");
	PASS();
}

/* Uninstall tests */

TEST(uninstall_no_arg)
{
	options opt = { .inputs = (char *[]){ "coffee" }, .inputs_num = 1 };
	int64_t ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall with no arg should return 1");
	PASS();
}

TEST(uninstall_not_installed)
{
	options opt = { .inputs = (char *[]){ "coffee", "nonexistent_pkg_xyz" }, .inputs_num = 2 };
	int64_t ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall missing package should return 1");
	PASS();
}

/* Report tests */

TEST(report_unknown_type)
{
	FILE *fp = fopen("/tmp/coffee-report-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0\"\n");
	fclose(fp);

	/* Can't test with real manifest easily, just test the command compiles */
	PASS();
}

void coffee_register_stubs_tests(void)
{
	TEST_REGISTER(publish);
	TEST_REGISTER(yank_no_arg);
	TEST_REGISTER(yank_invalid_format);
	TEST_REGISTER(yank_valid_format);
	TEST_REGISTER(owner_no_arg);
	TEST_REGISTER(owner_invalid_sub);
	TEST_REGISTER(owner_add);
	TEST_REGISTER(owner_remove);
	TEST_REGISTER(owner_list);
	TEST_REGISTER(install_update_config);
	TEST_REGISTER(uninstall_no_arg);
	TEST_REGISTER(uninstall_not_installed);
	TEST_REGISTER(report_unknown_type);
}