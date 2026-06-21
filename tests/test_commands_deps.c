/*
 * Tests for dependency-related commands that require a manifest
 * with dependencies.
 */
#include "../src/build.h"
#include "../src/coffee.h"
#include "../src/manifest.h"
#include "../src/project.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

/* Registration function forward declarations */
void coffee_register_commands_deps_tests(void);

/* ---- helpers ---- */

static char saved_cwd[4096];

static void setup_project(const char *test_name, const char *extra_toml)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coffee-cmd-deps-%s", test_name);
	mkdir(tmpdir, 0755);

	assert(getcwd(saved_cwd, sizeof(saved_cwd)) != nullptr);
	assert(chdir(tmpdir) == 0);

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp != nullptr);
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"%s\"\n", test_name);
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fprintf_safe(fp, "description = \"Test package\"\n");
	if (extra_toml) {
		fprintf_safe(fp, "%s", extra_toml);
	}
	fclose(fp);

	/* Create minimal src/ so build path works */
	mkdir("src", 0755);

	/* Create a dummy main.c so commands like build/run have source files */
	FILE *main_c = fopen("src/main.c", "w");
	if (main_c) {
		fprintf_safe(main_c, "int main(void) { return 0; }\n");
		fclose(main_c);
	}

	sdsfree(tmpdir);
}

static void teardown_project(const char *test_name)
{
	chdir(saved_cwd);
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coffee-cmd-deps-%s", test_name);
	sds cmd    = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
}

/* ---------------------------------------------------------------
 * add
 * --------------------------------------------------------------- */
TEST(add_no_package_name)
{
	options opt = {
		.inputs     = (char *[]){ "add" },
		.inputs_num = 1,
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 1, "add without package should return 1");
	PASS();
}

TEST(add_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "add", "somedep" },
		.inputs_num = 2,
	};
	i64 ret = handle_add(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "add without manifest should return 1");
	PASS();
}

TEST(add_with_manifest)
{
	setup_project("add-dep", nullptr);
	options opt = {
		.inputs     = (char *[]){ "add", "testdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_add(&opt);
	/* add will try to fetch from registry — may fail or succeed */
	teardown_project("add-dep");
	ASSERT(ret == 0 || ret == 1, "add should complete without crash");
	PASS();
}

/* ---------------------------------------------------------------
 * build
 * --------------------------------------------------------------- */
TEST(build_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-build-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "build" },
		.inputs_num = 1,
	};
	i64 ret = handle_build(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "build without manifest should return 1");
	PASS();
}

TEST(build_with_manifest_and_makefile)
{
	setup_project("build-make", nullptr);

	/* Create a Makefile so build uses it */
	FILE *fp = fopen("Makefile", "w");
	ASSERT(fp != nullptr, "could not create Makefile");
	fprintf_safe(fp, "all:\n\t@echo 'build ok'\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "build" },
		.inputs_num = 1,
	};
	i64 ret = handle_build(&opt);
	teardown_project("build-make");
	ASSERT(ret == 0, "build with Makefile should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * cflags
 * --------------------------------------------------------------- */
TEST(cflags_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-cflags-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "cflags" },
		.inputs_num = 1,
	};
	i64 ret = handle_cflags(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "cflags without manifest should return 1");
	PASS();
}

TEST(cflags_with_manifest)
{
	setup_project("cflags", nullptr);
	options opt = {
		.inputs     = (char *[]){ "cflags" },
		.inputs_num = 1,
	};
	i64 ret = handle_cflags(&opt);
	teardown_project("cflags");
	ASSERT(ret == 0, "cflags with manifest (no deps) should return 0");
	PASS();
}

TEST(cflags_by_package)
{
	options opt = {
		.inputs     = (char *[]){ "cflags", "nonexistent" },
		.inputs_num = 2,
	};
	/* cflags with a package name doesn't need manifest */
	i64 ret = handle_cflags(&opt);
	ASSERT(ret == 0, "cflags with nonexistent package should return 0 (empty output)");
	PASS();
}

/* ---------------------------------------------------------------
 * libs
 * --------------------------------------------------------------- */
TEST(libs_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-libs-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "libs" },
		.inputs_num = 1,
	};
	i64 ret = handle_libs(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "libs without manifest should return 1");
	PASS();
}

TEST(libs_with_manifest)
{
	setup_project("libs", nullptr);
	options opt = {
		.inputs     = (char *[]){ "libs" },
		.inputs_num = 1,
	};
	i64 ret = handle_libs(&opt);
	teardown_project("libs");
	ASSERT(ret == 0, "libs with manifest (no deps) should return 0");
	PASS();
}

TEST(libs_by_package)
{
	options opt = {
		.inputs     = (char *[]){ "libs", "nonexistent" },
		.inputs_num = 2,
	};
	i64 ret = handle_libs(&opt);
	ASSERT(ret == 0, "libs with nonexistent package should return 0 (empty output)");
	PASS();
}

/* ---------------------------------------------------------------
 * remove
 * --------------------------------------------------------------- */
TEST(remove_no_package)
{
	options opt = {
		.inputs     = (char *[]){ "remove" },
		.inputs_num = 1,
	};
	i64 ret = handle_remove(&opt);
	ASSERT(ret == 1, "remove without package should return 1");
	PASS();
}

TEST(remove_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-remove-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "remove", "somedep" },
		.inputs_num = 2,
	};
	i64 ret = handle_remove(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "remove without manifest should return 1");
	PASS();
}

TEST(remove_nonexistent_dep)
{
	setup_project("remove-nonexist", nullptr);
	options opt = {
		.inputs     = (char *[]){ "remove", "nonexistent_dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_remove(&opt);
	teardown_project("remove-nonexist");
	ASSERT(ret == 1, "remove of nonexistent dep should return 1");
	PASS();
}

/* ---------------------------------------------------------------
 * rm (delegates to remove)
 * --------------------------------------------------------------- */
TEST(rm_no_package)
{
	options opt = {
		.inputs     = (char *[]){ "rm" },
		.inputs_num = 1,
	};
	i64 ret = handle_rm(&opt);
	ASSERT(ret == 1, "rm without package should return 1 (same as remove)");
	PASS();
}

/* ---------------------------------------------------------------
 * run
 * --------------------------------------------------------------- */
TEST(run_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-run-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "run" },
		.inputs_num = 1,
	};
	i64 ret = handle_run(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "run without manifest should return 1");
	PASS();
}

TEST(run_with_manifest)
{
	setup_project("run", nullptr);

	/* Create a Makefile that actually builds the binary */
	FILE *fp = fopen("Makefile", "w");
	ASSERT(fp != nullptr, "could not create Makefile");
	fprintf_safe(fp, "CC := clang\n");
	fprintf_safe(fp, "CFLAGS := -std=c23 -O0 -g\n");
	fprintf_safe(fp, "TARGET := target/debug/run\n");
	fprintf_safe(fp, "all: $(TARGET)\n");
	fprintf_safe(fp, "$(TARGET): src/main.c\n");
	fprintf_safe(fp, "\t@mkdir -p target/debug\n");
	fprintf_safe(fp, "\t$(CC) $(CFLAGS) -o $(TARGET) src/main.c\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "run" },
		.inputs_num = 1,
	};
	i64 ret = handle_run(&opt);
	teardown_project("run");
	ASSERT(ret == 0, "run with Makefile should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * tree
 * --------------------------------------------------------------- */
TEST(tree_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-tree-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "tree" },
		.inputs_num = 1,
	};
	i64 ret = handle_tree(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "tree without manifest should return 1");
	PASS();
}

TEST(tree_no_deps)
{
	setup_project("tree-nodeps", nullptr);
	options opt = {
		.inputs     = (char *[]){ "tree" },
		.inputs_num = 1,
	};
	i64 ret = handle_tree(&opt);
	teardown_project("tree-nodeps");
	ASSERT(ret == 0, "tree with no deps should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * generate_lockfile
 * --------------------------------------------------------------- */
TEST(generate_lockfile_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-genlock-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "generate-lockfile" },
		.inputs_num = 1,
	};
	i64 ret = handle_generate_lockfile(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "generate-lockfile without manifest should return 1");
	PASS();
}

TEST(generate_lockfile_no_deps)
{
	setup_project("genlock-nodeps", nullptr);
	options opt = {
		.inputs     = (char *[]){ "generate-lockfile" },
		.inputs_num = 1,
	};
	i64 ret = handle_generate_lockfile(&opt);
	teardown_project("genlock-nodeps");
	ASSERT(ret == 0, "generate-lockfile with no deps should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * outdated
 * --------------------------------------------------------------- */
TEST(outdated_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-outdated-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "outdated" },
		.inputs_num = 1,
	};
	i64 ret = handle_outdated(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "outdated without manifest should return 1");
	PASS();
}

TEST(outdated_no_lockfile)
{
	setup_project("outdated-nolock", nullptr);
	options opt = {
		.inputs     = (char *[]){ "outdated" },
		.inputs_num = 1,
	};
	i64 ret = handle_outdated(&opt);
	teardown_project("outdated-nolock");
	/* outdated returns 0 when no lockfile exists (prints message) */
	ASSERT(ret == 0, "outdated without lockfile should return 0 (no crash)");
	PASS();
}

/* ---------------------------------------------------------------
 * update
 * --------------------------------------------------------------- */
TEST(update_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-update-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "update" },
		.inputs_num = 1,
	};
	i64 ret = handle_update(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "update without manifest should return 1");
	PASS();
}

TEST(update_no_deps)
{
	setup_project("update-nodeps", nullptr);
	options opt = {
		.inputs     = (char *[]){ "update" },
		.inputs_num = 1,
	};
	i64 ret = handle_update(&opt);
	teardown_project("update-nodeps");
	ASSERT(ret == 0, "update with no deps should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------- */
void coffee_register_commands_deps_tests(void)
{
	TEST_REGISTER(add_no_package_name);
	TEST_REGISTER(add_no_manifest);
	TEST_REGISTER(add_with_manifest);
	TEST_REGISTER(build_no_manifest);
	TEST_REGISTER(build_with_manifest_and_makefile);
	TEST_REGISTER(cflags_no_manifest);
	TEST_REGISTER(cflags_with_manifest);
	TEST_REGISTER(cflags_by_package);
	TEST_REGISTER(libs_no_manifest);
	TEST_REGISTER(libs_with_manifest);
	TEST_REGISTER(libs_by_package);
	TEST_REGISTER(remove_no_package);
	TEST_REGISTER(remove_no_manifest);
	TEST_REGISTER(remove_nonexistent_dep);
	TEST_REGISTER(rm_no_package);
	TEST_REGISTER(run_no_manifest);
	TEST_REGISTER(run_with_manifest);
	TEST_REGISTER(tree_no_manifest);
	TEST_REGISTER(tree_no_deps);
	TEST_REGISTER(generate_lockfile_no_manifest);
	TEST_REGISTER(generate_lockfile_no_deps);
	TEST_REGISTER(outdated_no_manifest);
	TEST_REGISTER(outdated_no_lockfile);
	TEST_REGISTER(update_no_manifest);
	TEST_REGISTER(update_no_deps);
}
