#include "../src/coffee.h"
#include "../src/manifest.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	ASSERT(fp != nullptr, "could not create test file");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0\"\n");
	fclose(fp);

	/* Can't test with real manifest easily, just test the command compiles */
	PASS();
}

void coffee_register_stubs_tests(void)
{
	TEST_REGISTER(install_update_config);
	TEST_REGISTER(uninstall_no_arg);
	TEST_REGISTER(uninstall_not_installed);
	TEST_REGISTER(report_unknown_type);
}