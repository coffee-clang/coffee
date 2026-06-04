/*
 * Coverage tests for install.c (package installation handler).
 *
 * Tests the handle_install function with basic argument validation
 * and the compile_binary helper with simple inputs.
 */

#include "../src/coffee.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_coverage_install_tests(void);

/* ---------- install with no package name ---------- */
TEST(cov_install_no_args)
{
	options opt = {
		.inputs     = (char *[]){ "install" },
		.inputs_num = 1,
	};
	i64 ret = handle_install(&opt);
	ASSERT(ret == 1, "install without args should fail");

	PASS();
}

/* ---------- install with unknown package ---------- */
TEST(cov_install_unknown_pkg)
{
	options opt = {
		.inputs     = (char *[]){ "install", "totally-nonexistent-pkg-xyz789" },
		.inputs_num = 2,
	};
	i64 ret = handle_install(&opt);
	ASSERT(ret == 1, "install unknown pkg should fail");

	PASS();
}

/* ---------- handle_install_update basic errors ---------- */
TEST(cov_install_update_no_args)
{
	/* Create a minimal project dir but don't chdir into it */
	options opt = {
		.inputs     = (char *[]){ "install-update" },
		.inputs_num = 1,
	};
	i64 ret = handle_install_update(&opt);
	/* handler updates all packages when no arg specified; returns 0 */
	ASSERT(ret == 0, "install-update without args returns 0");

	PASS();
}

void coffee_register_coverage_install_tests(void)
{
	TEST_REGISTER(cov_install_no_args);
	TEST_REGISTER(cov_install_unknown_pkg);
	TEST_REGISTER(cov_install_update_no_args);
}
