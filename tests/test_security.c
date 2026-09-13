/*
 * Security tests: run_command_capture, clean target-dir validation.
 */
#include "../src/build.h"
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
 * ref_is_valid / url_is_valid unit tests
 * --------------------------------------------------------------- */
TEST(ref_is_valid_unit)
{
	ASSERT(ref_is_valid("main"), "main should be valid");
	ASSERT(ref_is_valid("v1.0.0"), "v1.0.0 should be valid");
	ASSERT(ref_is_valid("refs/heads/main"), "refs/heads/main should be valid");
	ASSERT(ref_is_valid("origin/main"), "origin/main should be valid");
	ASSERT(ref_is_valid("abc123def"), "short sha should be valid");
	ASSERT(!ref_is_valid(nullptr), "nullptr should be invalid");
	ASSERT(!ref_is_valid(""), "empty should be invalid");
	ASSERT(!ref_is_valid("--upload-pack=sh -c 'id'"), "option injection should be invalid");
	ASSERT(!ref_is_valid("-x"), "leading dash should be invalid");
	ASSERT(!ref_is_valid("a b"), "spaces should be invalid");
	ASSERT(!ref_is_valid("a;b"), "semicolons should be invalid");
	ASSERT(!ref_is_valid("a$b"), "dollar should be invalid");
	PASS();
}

TEST(url_is_valid_unit)
{
	ASSERT(url_is_valid("https://github.com/x/y.git"), "https should be valid");
	ASSERT(url_is_valid("http://example.com/x"), "http should be valid");
	ASSERT(url_is_valid("git://example.com/x"), "git should be valid");
	ASSERT(url_is_valid("ssh://git@example.com/x"), "ssh should be valid");
	ASSERT(url_is_valid("git+ssh://git@example.com/x"), "git+ssh should be valid");
	ASSERT(url_is_valid("git+https://example.com/x"), "git+https should be valid");
	ASSERT(url_is_valid("/tmp/local-repo"), "absolute local path should be valid");
	ASSERT(url_is_valid("./local-repo"), "relative local path should be valid");
	ASSERT(!url_is_valid(nullptr), "nullptr should be invalid");
	ASSERT(!url_is_valid(""), "empty should be invalid");
	ASSERT(!url_is_valid("-o/tmp/pwned"), "option injection should be invalid");
	ASSERT(!url_is_valid("file:///etc/passwd"), "file scheme should be invalid");
	ASSERT(!url_is_valid("ftp://example.com"), "ftp scheme should be invalid");
	ASSERT(!url_is_valid("javascript:alert(1)"), "javascript scheme should be invalid");
	ASSERT(!url_is_valid("github.com/x/y"), "no scheme should be invalid");
	PASS();
}

/* ---------------------------------------------------------------
 * manifest — parse/write round-trip preserves inline-table deps
 * --------------------------------------------------------------- */
TEST(manifest_roundtrip_preserves_inline_deps)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-roundtrip");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "[dependencies]\n");
	fprintf_safe(fp, "mylib = { git = \"https://example.com/mylib.git\", branch = \"main\" }\n");
	fprintf_safe(fp, "mypath = { path = \"./lib\" }\n");
	fprintf_safe(fp, "plain = \"1.0\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest_parse should succeed");
	ASSERT(m->dependencies.deps_count == 2, "two structured deps should parse");
	ASSERT(manifest_write("Coffee.toml", m) == 0, "manifest_write should succeed");
	manifest_free(m);

	/* Re-parse the written file: git/path deps must survive */
	manifest_t *m2 = manifest_parse("Coffee.toml");
	ASSERT(m2 != nullptr, "re-parse should succeed");
	ASSERT(m2->dependencies.deps_count == 2, "two structured deps should survive round-trip");
	bool found_git   = false;
	bool found_path  = false;
	bool found_plain = false;
	for (size_t i = 0; i < m2->dependencies.deps_count; i++) {
		dependency_t *d = &m2->dependencies.deps[i];
		if (d->name != nullptr && strcmp(d->name, "mylib") == 0 && d->git != nullptr &&
		    strcmp(d->git, "https://example.com/mylib.git") == 0 && d->branch != nullptr &&
		    strcmp(d->branch, "main") == 0) {
			found_git = true;
		}
		if (d->name != nullptr && strcmp(d->name, "mypath") == 0 && d->path != nullptr &&
		    strcmp(d->path, "./lib") == 0) {
			found_path = true;
		}
	}
	for (size_t i = 0; i < m2->package.dependencies_count; i++) {
		if (m2->package.dependencies[i] != nullptr &&
		    strncmp(m2->package.dependencies[i], "plain", 5) == 0) {
			found_plain = true;
		}
	}
	ASSERT(found_git, "git dep should survive round-trip");
	ASSERT(found_path, "path dep should survive round-trip");
	ASSERT(found_plain, "plain dep should survive round-trip");
	manifest_free(m2);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * registry — mock index parsing (no network)
 * --------------------------------------------------------------- */
TEST(registry_search_parses_mock_index)
{
	sds tmpdir = sdsnew("/tmp/coffee-test-registry");
	sds rmcmd  = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(rmcmd);
	sdsfree(rmcmd);
	mkdir(tmpdir, 0755);
	setenv("COFFEE_HOME", tmpdir, 1);

	FILE *fp = fopen("/tmp/coffee-test-registry/packages.json", "w");
	ASSERT(fp != nullptr, "fopen mock index failed");
	/* The registry parser scans line-by-line, so keys must start lines. */
	fprintf_safe(fp, "{\n\"name\": \"alpha\",\n\"version\": \"1.0.0\",\n\"description\": \"test pkg\"\n}\n");
	fclose(fp);

	recipe_list_t *list = registry_search(sdsnew("alpha"));
	ASSERT(list != nullptr, "search should return a list");
	ASSERT(list->count == 1, "one recipe should match");
	if (list->count > 0) {
		ASSERT(list->recipes[0].name != nullptr, "recipe name present");
		ASSERT(strcmp(list->recipes[0].name, "alpha") == 0, "recipe name matches");
	}
	registry_free_recipes(list);

	remove("/tmp/coffee-test-registry/packages.json");
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * safe_strtol / safe_atol unit tests
 * --------------------------------------------------------------- */
TEST(safe_strtol_valid)
{
	i64 val = 0;
	ASSERT(safe_strtol("42", 10, &val), "42 should parse");
	ASSERT(val == 42, "42 should parse to 42");
	ASSERT(safe_strtol("  -7", 10, &val), "leading whitespace before -7 should parse");
	ASSERT(val == -7, "-7 should parse to -7");
	ASSERT(!safe_strtol("42 ", 10, &val), "trailing whitespace should be rejected");
	ASSERT(safe_strtol("ff", 16, &val), "ff base 16 should parse");
	ASSERT(val == 255, "ff base 16 should be 255");
	PASS();
}

TEST(safe_strtol_invalid)
{
	i64 val = 0;
	ASSERT(!safe_strtol(nullptr, 10, &val), "nullptr should be rejected");
	ASSERT(!safe_strtol("", 10, &val), "empty string should be rejected");
	ASSERT(!safe_strtol("abc", 10, &val), "garbage should be rejected");
	ASSERT(!safe_strtol("42x", 10, &val), "trailing garbage should be rejected");
	ASSERT(!safe_strtol("999999999999999999999", 10, &val), "overflow should be rejected");
	ASSERT(!safe_strtol("--5", 10, &val), "double sign should be rejected");
	PASS();
}

TEST(safe_atol_valid)
{
	i64 val = 0;
	ASSERT(safe_atol("123", &val), "123 should parse");
	ASSERT(val == 123, "123 should parse to 123");
	ASSERT(safe_atol("0", &val), "0 should parse");
	ASSERT(val == 0, "0 should parse to 0");
	PASS();
}

TEST(safe_atol_invalid)
{
	i64 val = 0;
	ASSERT(!safe_atol("12x", &val), "trailing garbage should be rejected");
	ASSERT(!safe_atol("", &val), "empty string should be rejected");
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
	TEST_REGISTER(ref_is_valid_unit);
	TEST_REGISTER(url_is_valid_unit);
	TEST_REGISTER(manifest_roundtrip_preserves_inline_deps);
	TEST_REGISTER(registry_search_parses_mock_index);
	TEST_REGISTER(fetch_rejects_dotdot_dep);
	TEST_REGISTER(fetch_rejects_slash_dep);
	TEST_REGISTER(manifest_rejects_traversal_name);
	TEST_REGISTER(safe_strtol_valid);
	TEST_REGISTER(safe_strtol_invalid);
	TEST_REGISTER(safe_atol_valid);
	TEST_REGISTER(safe_atol_invalid);
}
