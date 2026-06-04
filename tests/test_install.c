#include "../src/coffee.h"
#include "../src/manifest.h"
#include "../src/project.h"
#include "../src/registry.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_install_tests(void);

/*
 * Install/list tests use a temporary COFFEE_HOME to avoid test pollution.
 */

static char saved_coffee_home[4096];

static void set_test_home(const char *test_name)
{
	const char *old = getenv("COFFEE_HOME");
	if (old) {
		snprintf_safe(saved_coffee_home, sizeof(saved_coffee_home), "%s", old);
	} else {
		saved_coffee_home[0] = '\0';
	}

	sds test_home = sdscatprintf(sdsempty(), "/tmp/coffee-install-%s", test_name);
	setenv("COFFEE_HOME", test_home, 1);
	mkdir(test_home, 0755);
	sdsfree(test_home);
}

static void restore_home(void)
{
	if (saved_coffee_home[0]) {
		setenv("COFFEE_HOME", saved_coffee_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
}

TEST(install_list_installed_empty)
{
	set_test_home("list-installed-empty");

	/* Inputs: ["list", "installed"] — mimicking dispatch when `coffee list installed` is run */
	options opt = {
		.inputs     = (char *[]){ "list", "installed" },
		.inputs_num = 2,
	};

	FILE *old = stdout;
	FILE *tmp = fopen("/tmp/coffee-list-out.txt", "w");
	stdout    = tmp;

	i64 ret = handle_list(&opt);

	fclose(tmp);
	stdout = old;

	restore_home();

	ASSERT(ret == 0, "list installed with no binaries should return 0");
	remove("/tmp/coffee-list-out.txt");
	PASS();
}

TEST(install_list_deps_no_manifest)
{
	set_test_home("list-deps-no-manifest");

	/* chdir to a temp dir with no Coffee.toml */
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-install-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");
	sdsfree(tmpdir);

	options opt = {
		.inputs     = (char *[]){ "list" },
		.inputs_num = 1,
	};
	i64 ret = handle_list(&opt);

	chdir(old_cwd);
	remove("/tmp/coffee-install-nomanifest");
	restore_home();

	ASSERT(ret == 1, "list without manifest should return 1");
	PASS();
}

TEST(install_list_project_with_bin)
{
	set_test_home("list-project-with-bin");

	/* Create a temp project with a Coffee.toml that has [[bin]] */
	sds tmpdir = sdsnew("/tmp/coffee-install-list-project");
	mkdir(tmpdir, 0755);

	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", tmpdir);
	FILE *fp        = fopen(toml_path, "w");
	ASSERT(fp != nullptr, "could not create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"listtest\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "\n[[bin]]\n");
	fprintf_safe(fp, "name = \"mytool\"\n");
	fprintf_safe(fp, "src = [\"src/main.c\"]\n");
	fclose(fp);

	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "list" },
		.inputs_num = 1,
	};

	FILE *old = stdout;
	FILE *tmp = fopen("/tmp/coffee-list-out2.txt", "w");
	stdout    = tmp;

	i64 ret = handle_list(&opt);

	fclose(tmp);
	stdout = old;

	/* Read captured output */
	fp = fopen("/tmp/coffee-list-out2.txt", "r");
	ASSERT(fp != nullptr, "should have output file");
	char   buf[4096];
	size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[n]   = '\0';
	fclose(fp);
	remove("/tmp/coffee-list-out2.txt");

	/* Cleanup before asserts that might fail */
	remove(toml_path);
	chdir(old_cwd);
	remove(tmpdir);

	i64 has_name = (strstr(buf, "listtest") != nullptr);
	i64 has_bin  = (strstr(buf, "mytool") != nullptr);

	ASSERT(ret == 0, "list with manifest should return 0");
	ASSERT(has_name, "should contain package name");
	ASSERT(has_bin, "should contain binary name");

	sdsfree(tmpdir);
	sdsfree(toml_path);
	restore_home();
	PASS();
}

void coffee_register_install_tests(void)
{
	TEST_REGISTER(install_list_installed_empty);
	TEST_REGISTER(install_list_deps_no_manifest);
	TEST_REGISTER(install_list_project_with_bin);
}
