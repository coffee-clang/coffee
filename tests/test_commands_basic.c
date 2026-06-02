/*
 * Tests for commands that do not require a Coffee.toml manifest.
 */
#include "../src/coffee.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

/* Registration function forward declarations */
void coffee_register_commands_basic_tests(void);

/* ---------------------------------------------------------------
 * clean
 * --------------------------------------------------------------- */
TEST(clean_basic)
{
	/* Create a dummy target directory to clean */
	mkdir("target", 0755);
	FILE *fp = fopen("target/dummy.txt", "w");
	if (fp) {
		fprintf_safe(fp, "test\n");
		fclose(fp);
	}

	options opt = {
		.inputs     = (char *[]){ "clean" },
		.inputs_num = 1,
	};
	i64 ret = handle_clean(&opt);
	ASSERT(ret == 0, "clean should return 0");
	ASSERT(access("target", F_OK) != 0, "target dir should be removed after clean");
	PASS();
}

TEST(clean_custom_target_dir)
{
	/* Create custom target dir */
	mkdir("mybuild", 0755);
	FILE *fp = fopen("mybuild/dummy.txt", "w");
	if (fp) {
		fprintf_safe(fp, "test\n");
		fclose(fp);
	}

	options opt = {
		.inputs     = (char *[]){ "clean" },
		.inputs_num = 1,
		.target_dir = "mybuild",
	};
	i64 ret = handle_clean(&opt);
	ASSERT(ret == 0, "clean with custom target_dir should return 0");
	ASSERT(access("mybuild", F_OK) != 0, "mybuild dir should be removed after clean");
	PASS();
}

/* ---------------------------------------------------------------
 * compile (delegates to build)
 * --------------------------------------------------------------- */
TEST(compile_delegates_to_build)
{
	/* compile without manifest should return error like build does */
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	sds tmpdir = sdsnew("/tmp/coffee-test-compile");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "compile" },
		.inputs_num = 1,
	};
	i64 ret = handle_compile(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "compile without manifest should return 1 (same as build)");
	PASS();
}

/* ---------------------------------------------------------------
 * fmt
 * --------------------------------------------------------------- */
TEST(fmt_basic)
{
	/* fmt runs clang-format on src/tests — just check it returns 0 */
	options opt = {
		.inputs     = (char *[]){ "fmt" },
		.inputs_num = 1,
	};
	i64 ret = handle_fmt(&opt);
	ASSERT(ret == 0, "fmt should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * grep
 * --------------------------------------------------------------- */
TEST(grep_no_pattern)
{
	options opt = {
		.inputs     = (char *[]){ "grep" },
		.inputs_num = 1,
	};
	i64 ret = handle_grep(&opt);
	ASSERT(ret == 1, "grep without pattern should return 1");
	PASS();
}

TEST(grep_with_pattern)
{
	options opt = {
		.inputs     = (char *[]){ "grep", "handle_" },
		.inputs_num = 2,
	};
	i64 ret = handle_grep(&opt);
	ASSERT(ret == 0, "grep with existing pattern should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * help
 * --------------------------------------------------------------- */
TEST(help_basic)
{
	options opt = {
		.inputs     = (char *[]){ "help" },
		.inputs_num = 1,
	};
	i64 ret = handle_help(&opt);
	ASSERT(ret == 0, "help should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * init
 * --------------------------------------------------------------- */
TEST(init_initializes_project)
{
	char old_cwd[4096];
	sds  tmpdir = sdsnew("/tmp/coffee-test-init");
	mkdir(tmpdir, 0755);
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "init" },
		.inputs_num = 1,
	};
	i64 ret = handle_init(&opt);

	/* Check files were created */
	i64 has_toml = (access("Coffee.toml", F_OK) == 0);
	i64 has_src  = (access("src", F_OK) == 0);
	i64 has_main = (access("src/main.c", F_OK) == 0);

	/* Cleanup */
	chdir(old_cwd);

	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);

	ASSERT(ret == 0, "init should return 0");
	ASSERT(has_toml, "init should create Coffee.toml");
	ASSERT(has_src, "init should create src/");
	ASSERT(has_main, "init should create src/main.c");
	PASS();
}

/* ---------------------------------------------------------------
 * install_update (update command with no packages)
 * --------------------------------------------------------------- */
TEST(install_update_no_packages)
{
	options opt = {
		.inputs     = (char *[]){ "update" },
		.inputs_num = 1,
	};
	i64 ret = handle_install_update(&opt);
	ASSERT(ret == 0, "install_update with no packages should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * install_update_config (stub)
 * --------------------------------------------------------------- */
TEST(install_update_config_stub)
{
	options opt = {
		.inputs     = (char *[]){ "install-update-config" },
		.inputs_num = 1,
	};
	i64 ret = handle_install_update_config(&opt);
	ASSERT(ret == 1, "install-update-config should return 1 (not yet supported)");
	PASS();
}

/* ---------------------------------------------------------------
 * logout
 * --------------------------------------------------------------- */
TEST(logout_not_logged_in)
{
	options opt = {
		.inputs     = (char *[]){ "logout" },
		.inputs_num = 1,
	};
	i64 ret = handle_logout(&opt);
	ASSERT(ret == 0, "logout when not logged in should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * new
 * --------------------------------------------------------------- */
TEST(new_creates_project)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	sds tmpdir = sdsnew("/tmp/coffee-test-new-project");
	mkdir(tmpdir, 0755);

	options opt = {
		.inputs     = (char *[]){ "new", tmpdir },
		.inputs_num = 2,
	};
	i64 ret = handle_new(&opt);

	/* Check files were created */
	i64 has_toml = (access(sdscatprintf(sdsempty(), "%s/Coffee.toml", tmpdir), F_OK) == 0);

	/* Cleanup */
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);

	ASSERT(ret == 0, "new should return 0");
	ASSERT(has_toml, "new should create Coffee.toml");
	PASS();
}

TEST(new_no_arg)
{
	options opt = {
		.inputs     = (char *[]){ "new" },
		.inputs_num = 1,
	};
	i64 ret = handle_new(&opt);
	ASSERT(ret == 1, "new without path should return 1");
	PASS();
}

/* ---------------------------------------------------------------
 * search
 * --------------------------------------------------------------- */
TEST(search_no_query)
{
	options opt = {
		.inputs     = (char *[]){ "search" },
		.inputs_num = 1,
	};
	i64 ret = handle_search(&opt);
	/* search with empty query should return 0 (prints all packages) */
	ASSERT(ret == 0, "search without query should return 0");
	PASS();
}

TEST(search_with_query)
{
	options opt = {
		.inputs     = (char *[]){ "search", "json" },
		.inputs_num = 2,
	};
	i64 ret = handle_search(&opt);
	ASSERT(ret == 0, "search with query should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * uninstall
 * --------------------------------------------------------------- */
TEST(uninstall_no_arg)
{
	options opt = {
		.inputs     = (char *[]){ "uninstall" },
		.inputs_num = 1,
	};
	i64 ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall without package should return 1");
	PASS();
}

TEST(uninstall_not_installed)
{
	options opt = {
		.inputs     = (char *[]){ "uninstall", "nonexistent_pkg_xyz" },
		.inputs_num = 2,
	};
	i64 ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall of non-existent package should return 1");
	PASS();
}

/* ---------------------------------------------------------------
 * version
 * --------------------------------------------------------------- */
TEST(version_basic)
{
	options opt = {
		.inputs     = (char *[]){ "version" },
		.inputs_num = 1,
	};
	i64 ret = handle_version(&opt);
	ASSERT(ret == 0, "version should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------- */
void coffee_register_commands_basic_tests(void)
{
	TEST_REGISTER(clean_basic);
	TEST_REGISTER(clean_custom_target_dir);
	TEST_REGISTER(compile_delegates_to_build);
	TEST_REGISTER(fmt_basic);
	TEST_REGISTER(grep_no_pattern);
	TEST_REGISTER(grep_with_pattern);
	TEST_REGISTER(help_basic);
	TEST_REGISTER(init_initializes_project);
	TEST_REGISTER(install_update_no_packages);
	TEST_REGISTER(install_update_config_stub);
	TEST_REGISTER(logout_not_logged_in);
	TEST_REGISTER(new_creates_project);
	TEST_REGISTER(new_no_arg);
	TEST_REGISTER(search_no_query);
	TEST_REGISTER(search_with_query);
	TEST_REGISTER(uninstall_no_arg);
	TEST_REGISTER(uninstall_not_installed);
	TEST_REGISTER(version_basic);
}
