/*
 * Security tests: run_command_capture, clean target-dir validation.
 */
#include "../src/build.h"
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

void coffee_register_security_tests(void);

/* ---------------------------------------------------------------
 * run_command_capture
 * --------------------------------------------------------------- */
TEST(run_command_capture_echo)
{
	char *argv[] = { "echo", "hello world", nullptr };
	sds   out    = run_command_capture(argv, RUN_CMD_QUIET);
	ASSERT(out != nullptr, "run_command_capture should succeed for echo");
	ASSERT(sdslen(out) > 0, "output should be non-empty");
	ASSERT(strncmp(out, "hello world", 11) == 0, "output should start with 'hello world'");
	sdsfree(out);
	PASS();
}

TEST(run_command_capture_failure)
{
	char *argv[] = { "false", nullptr };
	sds   out    = run_command_capture(argv, RUN_CMD_QUIET);
	ASSERT(out == nullptr, "run_command_capture should return nullptr for failing command");
	PASS();
}

TEST(run_command_capture_nonexistent)
{
	char *argv[] = { "/nonexistent/binary/xyz", nullptr };
	sds   out    = run_command_capture(argv, RUN_CMD_QUIET);
	ASSERT(out == nullptr, "run_command_capture should return nullptr for missing binary");
	PASS();
}

TEST(run_command_capture_multiline)
{
	char *argv[] = { "printf", "line1\nline2\nline3", nullptr };
	sds   out    = run_command_capture(argv, RUN_CMD_QUIET);
	ASSERT(out != nullptr, "run_command_capture should succeed for printf");
	ASSERT(strstr(out, "line1") != nullptr, "output contains line1");
	ASSERT(strstr(out, "line2") != nullptr, "output contains line2");
	ASSERT(strstr(out, "line3") != nullptr, "output contains line3");
	sdsfree(out);
	PASS();
}

/* ---------------------------------------------------------------
 * clean — unsafe target-dir rejection
 * --------------------------------------------------------------- */
TEST(clean_rejects_absolute_path)
{
	options opt = {
		.inputs     = (char *[]){ "clean" },
		.inputs_num = 1,
		.target_dir = "/tmp/should-not-be-cleaned",
	};
	i64 ret = handle_clean(&opt);
	ASSERT(ret != 0, "clean should reject absolute target_dir");
	PASS();
}

TEST(clean_rejects_dotdot)
{
	options opt = {
		.inputs     = (char *[]){ "clean" },
		.inputs_num = 1,
		.target_dir = "../parent",
	};
	i64 ret = handle_clean(&opt);
	ASSERT(ret != 0, "clean should reject ../parent target_dir");
	PASS();
}

TEST(clean_rejects_bare_dotdot)
{
	options opt = {
		.inputs     = (char *[]){ "clean" },
		.inputs_num = 1,
		.target_dir = "..",
	};
	i64 ret = handle_clean(&opt);
	ASSERT(ret != 0, "clean should reject .. target_dir");
	PASS();
}

TEST(clean_accepts_relative_path)
{
	mkdir("test_clean_rel", 0755);
	options opt = {
		.inputs     = (char *[]){ "clean" },
		.inputs_num = 1,
		.target_dir = "test_clean_rel",
	};
	i64 ret = handle_clean(&opt);
	ASSERT(ret == 0, "clean should accept relative target_dir");
	ASSERT(access("test_clean_rel", F_OK) != 0, "test_clean_rel should be removed");
	PASS();
}

TEST(clean_accepts_dotdot_prefix_name)
{
	mkdir("..config", 0755);
	options opt = {
		.inputs     = (char *[]){ "clean" },
		.inputs_num = 1,
		.target_dir = "..config",
	};
	i64 ret = handle_clean(&opt);
	ASSERT(ret == 0, "clean should accept ..config (not a traversal)");
	ASSERT(access("..config", F_OK) != 0, "..config should be removed");
	PASS();
}

/* ---------------------------------------------------------------
 * dep_name_is_valid unit tests
 * --------------------------------------------------------------- */
TEST(dep_name_is_valid_unit)
{
	ASSERT(!dep_name_is_valid(nullptr), "nullptr should be invalid");
	ASSERT(!dep_name_is_valid(""), "empty string should be invalid");
	ASSERT(!dep_name_is_valid("."), ". should be invalid");
	ASSERT(!dep_name_is_valid(".."), ".. should be invalid");
	ASSERT(!dep_name_is_valid("../etc"), "../etc should be invalid");
	ASSERT(!dep_name_is_valid("foo/bar"), "foo/bar should be invalid");
	ASSERT(!dep_name_is_valid("a/b"), "a/b should be invalid");
	ASSERT(dep_name_is_valid("foo"), "foo should be valid");
	ASSERT(dep_name_is_valid("..config"), "..config should be valid");
	ASSERT(dep_name_is_valid("foo-bar"), "foo-bar should be valid");
	ASSERT(dep_name_is_valid("mylib"), "mylib should be valid");
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — traversal rejection
 * --------------------------------------------------------------- */
TEST(fetch_rejects_dotdot_dep)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-fetch-dotdot");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "[dependencies]\n\"../evil\" = { path = \".\" }\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};
	i64 ret = handle_fetch(&opt);

	ASSERT(ret == 0, "fetch should succeed (invalid dep skipped)");
	ASSERT(access("evil", F_OK) != 0, "no traversal symlink should exist inside tmpdir");

	remove("Coffee.toml");
	remove("Coffee.lock");
	rmdir("deps");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

TEST(fetch_rejects_slash_dep)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-fetch-slash");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "[dependencies]\n\"foo/bar\" = { path = \".\" }\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};
	i64 ret = handle_fetch(&opt);

	ASSERT(ret == 0, "fetch should succeed (invalid dep skipped)");
	ASSERT(access("deps/foo", F_OK) != 0, "no deps/foo should exist");
	ASSERT(access("deps/foo/bar", F_OK) != 0, "no deps/foo/bar should exist");

	remove("Coffee.toml");
	remove("Coffee.lock");
	rmdir("deps");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * manifest — traversal name rejection at parse time
 * --------------------------------------------------------------- */
TEST(manifest_rejects_traversal_name)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-manifest-traversal");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "[dependencies]\nnormal = { path = \".\" }\n\"../evil\" = { path = \".\" }\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest_parse should succeed (valid entries still parse)");
	ASSERT(m->dependencies.deps_count == 1, "only one valid dep should remain");
	ASSERT(m->dependencies.deps[0].name != nullptr, "valid dep should have name");
	ASSERT(strcmp(m->dependencies.deps[0].name, "normal") == 0, "remaining dep should be 'normal'");
	manifest_free(m);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);

	PASS();
}
void coffee_register_security_tests(void)
{
	TEST_REGISTER(run_command_capture_echo);
	TEST_REGISTER(run_command_capture_failure);
	TEST_REGISTER(run_command_capture_nonexistent);
	TEST_REGISTER(run_command_capture_multiline);
	TEST_REGISTER(clean_rejects_absolute_path);
	TEST_REGISTER(clean_rejects_dotdot);
	TEST_REGISTER(clean_rejects_bare_dotdot);
	TEST_REGISTER(clean_accepts_relative_path);
	TEST_REGISTER(clean_accepts_dotdot_prefix_name);
	TEST_REGISTER(dep_name_is_valid_unit);
	TEST_REGISTER(fetch_rejects_dotdot_dep);
	TEST_REGISTER(fetch_rejects_slash_dep);
	TEST_REGISTER(manifest_rejects_traversal_name);
}
