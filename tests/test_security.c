/*
 * Security tests: run_command_capture, clean target-dir validation.
 */
#include "../src/build.h"
#include "../src/coffee.h"
#include "../src/lockfile.h"
#include "../src/manifest.h"
#include "../src/project.h"
#include "../src/registry.h"
#include "../src/strings.h"
#include "../src/toolcheck.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

void coffee_register_security_tests(void);

/* Defined below; used by the add-injection test. */
static sds read_file(const char *path);

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
	/* The name is also emitted as a TOML bare key, so anything outside
	 * [A-Za-z0-9_-] must be rejected. */
	ASSERT(!dep_name_is_valid("..config"), "..config should be invalid (dot is not a bare-key char)");
	ASSERT(!dep_name_is_valid("a.b"), "a.b should be invalid");
	ASSERT(!dep_name_is_valid("a b"), "space should be invalid");
	ASSERT(!dep_name_is_valid("a=b"), "= should be invalid");
	ASSERT(!dep_name_is_valid("a\"b"), "double quote should be invalid");
	ASSERT(!dep_name_is_valid("a#b"), "# should be invalid");
	ASSERT(!dep_name_is_valid("a[b]"), "brackets should be invalid");
	ASSERT(!dep_name_is_valid("a\nb"), "newline should be invalid");
	ASSERT(dep_name_is_valid("foo"), "foo should be valid");
	ASSERT(dep_name_is_valid("foo-bar"), "foo-bar should be valid");
	ASSERT(dep_name_is_valid("mylib"), "mylib should be valid");
	ASSERT(dep_name_is_valid("_x"), "leading underscore should be valid");
	ASSERT(dep_name_is_valid("A-Z_09"), "mixed charset should be valid");
	ASSERT(dep_name_is_valid("json-c"), "json-c should be valid");
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — traversal rejection
 * --------------------------------------------------------------- */
TEST(fetch_rejects_dotdot_dep)
{
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
 * add — input validation (TOML injection)
 * --------------------------------------------------------------- */
TEST(add_rejects_injection_inputs)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-injection");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	sds before = read_file("Coffee.toml");

	/* A name that would close the bare key and inject a new one. */
	options opt1 = {
		.inputs     = (char *[]){ "add", "x\" = { evil = \"1\" }" },
		.inputs_num = 2,
		.path       = sdsnew("."),
	};
	ASSERT(handle_add(&opt1) == 1, "quoted-key injection name should be rejected");
	sdsfree(opt1.path);

	/* A name containing a newline. */
	options opt2 = {
		.inputs     = (char *[]){ "add", "x\nevil = 1" },
		.inputs_num = 2,
		.path       = sdsnew("."),
	};
	ASSERT(handle_add(&opt2) == 1, "newline injection name should be rejected");
	sdsfree(opt2.path);

	/* A name with a space and an '='. */
	options opt3 = {
		.inputs     = (char *[]){ "add", "x = 1" },
		.inputs_num = 2,
		.path       = sdsnew("."),
	};
	ASSERT(handle_add(&opt3) == 1, "bare-key injection name should be rejected");
	sdsfree(opt3.path);

	/* Valid name, hostile source or version. */
	options opt4 = {
		.inputs     = (char *[]){ "add", "mylib" },
		.inputs_num = 2,
		.git        = sdsnew("-o/tmp/pwned"),
	};
	ASSERT(handle_add(&opt4) == 1, "option-injection git URL should be rejected");
	sdsfree(opt4.git);

	options opt5 = {
		.inputs     = (char *[]){ "add", "mylib" },
		.inputs_num = 2,
		.path       = sdsnew("file:///etc/passwd"),
	};
	ASSERT(handle_add(&opt5) == 1, "file:// path should be rejected");
	sdsfree(opt5.path);

	options opt6 = {
		.inputs      = (char *[]){ "add", "mylib" },
		.inputs_num  = 2,
		.path        = sdsnew("./lib"),
		.pkg_version = sdsnew("1.0.0/../../x"),
	};
	ASSERT(handle_add(&opt6) == 1, "traversal version should be rejected");
	sdsfree(opt6.path);
	sdsfree(opt6.pkg_version);

	/* Every rejected call must leave the manifest untouched. */
	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest should still parse");
	ASSERT(m->dependencies.deps_count == 0, "no structured dependency should have been written");
	ASSERT(m->package.dependencies_count == 0, "no flat dependency should have been written");
	manifest_free(m);

	sds after = read_file("Coffee.toml");
	ASSERT(strcmp(before, after) == 0, "rejected add must leave Coffee.toml byte-identical");
	sdsfree(after);
	sdsfree(before);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

TEST(add_accepts_valid_path_dep)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-valid");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "add", "mylib" },
		.inputs_num = 2,
		.path       = sdsnew("./lib"),
	};
	i64 ret = handle_add(&opt);
	sdsfree(opt.path);
	ASSERT(ret == 0, "add with a valid path dep should succeed");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest should parse after add");
	ASSERT(m->dependencies.deps_count == 1, "one structured dep should be written");
	bool found = false;
	if (m->dependencies.deps_count == 1) {
		dependency_t *d = &m->dependencies.deps[0];
		found =
		    d->name != nullptr && strcmp(d->name, "mylib") == 0 && d->path != nullptr && strcmp(d->path, "./lib") == 0;
	}
	ASSERT(found, "mylib path dep should round-trip");
	manifest_free(m);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * add — values that are not emitted are not validated
 * --------------------------------------------------------------- */
TEST(add_ignores_unemitted_values)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-ignored");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	/* --path wins over --git, so a hostile --git is never emitted and
	 * must not be rejected. */
	options opt1 = {
		.inputs     = (char *[]){ "add", "pathwins" },
		.inputs_num = 2,
		.path       = sdsnew("./lib"),
		.git        = sdsnew("-o/tmp/pwned"),
	};
	ASSERT(handle_add(&opt1) == 0, "ignored --git should not be validated");
	sdsfree(opt1.path);
	sdsfree(opt1.git);

	/* --pkg-version is written only for a path dep. */
	options opt2 = {
		.inputs      = (char *[]){ "add", "gitdep" },
		.inputs_num  = 2,
		.git         = sdsnew("https://example.com/r.git"),
		.pkg_version = sdsnew("bad ver"),
	};
	ASSERT(handle_add(&opt2) == 0, "ignored --pkg-version should not be validated");
	sdsfree(opt2.git);
	sdsfree(opt2.pkg_version);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest should parse");
	ASSERT(m->dependencies.deps_count == 2, "both deps should be written");
	bool found_pathwins = false;
	bool found_gitdep   = false;
	for (size_t i = 0; i < m->dependencies.deps_count; i++) {
		dependency_t *d = &m->dependencies.deps[i];
		if (d->name == nullptr) {
			continue;
		}
		if (strcmp(d->name, "pathwins") == 0) {
			/* The emitted source must be the --path value, not the
			 * hostile --git that was ignored. */
			found_pathwins = d->path != nullptr && strcmp(d->path, "./lib") == 0 && d->git == nullptr;
		}
		if (strcmp(d->name, "gitdep") == 0) {
			/* The ignored --pkg-version must not be written. */
			found_gitdep =
			    d->git != nullptr && strcmp(d->git, "https://example.com/r.git") == 0 && d->version == nullptr;
		}
	}
	ASSERT(found_pathwins, "pathwins should carry the --path value and no git");
	ASSERT(found_gitdep, "gitdep should carry the --git value and no version");
	manifest_free(m);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * add — emitted values are escaped, so the file stays parseable
 * --------------------------------------------------------------- */
TEST(add_escapes_emitted_values)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-escape");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	/* A double quote and a control character in the path: both must be
	 * escaped, or the written file is unparseable. */
	options opt = {
		.inputs     = (char *[]){ "add", "escaped" },
		.inputs_num = 2,
		.path       = sdsnew("./li\"b\x01"),
	};
	ASSERT(handle_add(&opt) == 0, "path with quote and control char should be accepted");
	sdsfree(opt.path);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest with escaped value should parse");
	ASSERT(m->dependencies.deps_count == 1, "one dep should be written");
	bool found = false;
	if (m->dependencies.deps_count == 1) {
		dependency_t *d = &m->dependencies.deps[0];
		found           = d->name != nullptr && strcmp(d->name, "escaped") == 0 && d->path != nullptr &&
		                  strcmp(d->path, "./li\"b\x01") == 0;
	}
	ASSERT(found, "escaped path should round-trip byte-for-byte");
	manifest_free(m);

	remove("Coffee.toml");
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
	ASSERT(url_is_valid("git://example.com/x"), "git should be valid");
	ASSERT(url_is_valid("ssh://git@example.com/x"), "ssh should be valid");
	ASSERT(url_is_valid("git+ssh://git@example.com/x"), "git+ssh should be valid");
	ASSERT(url_is_valid("git+https://example.com/x"), "git+https should be valid");
	ASSERT(url_is_valid("/tmp/local-repo"), "absolute local path should be valid");
	ASSERT(url_is_valid("./local-repo"), "relative local path should be valid");
	ASSERT(url_is_valid("../local-repo"), "parent-relative local path should be valid");
	ASSERT(url_is_valid("git@github.com:org/repo.git"), "scp form should be valid");
	ASSERT(url_is_valid("git@host:~/path"), "scp form with tilde should be valid");
	ASSERT(!url_is_valid(nullptr), "nullptr should be invalid");
	ASSERT(!url_is_valid(""), "empty should be invalid");
	ASSERT(!url_is_valid("http://example.com/x"), "http scheme should be invalid");
	ASSERT(!url_is_valid("HTTPS://example.com/x"), "uppercase https scheme should be invalid");
	ASSERT(!url_is_valid("-o/tmp/pwned"), "option injection should be invalid");
	ASSERT(!url_is_valid("file:///etc/passwd"), "file scheme should be invalid");
	ASSERT(!url_is_valid("ftp://example.com"), "ftp scheme should be invalid");
	ASSERT(!url_is_valid("javascript:alert(1)"), "javascript scheme should be invalid");
	ASSERT(!url_is_valid("github.com/x/y"), "no scheme should be invalid");
	/* A non-allow-listed scheme embedding '@' and ':' must not slip
	 * through the scp-style fallback. */
	ASSERT(!url_is_valid("http://user@host:8080/x"), "http with @ and port should be invalid");
	ASSERT(!url_is_valid("ftp://u@h:1/x"), "ftp with @ and port should be invalid");
	ASSERT(!url_is_valid("git@host:;rm -rf /"), "scp path with metacharacters should be invalid");
	ASSERT(!url_is_valid("git@host:-o/tmp/x"), "scp path with leading dash should be invalid");
	ASSERT(!url_is_valid("git@host:"), "scp form with empty path should be invalid");
	ASSERT(!url_is_valid("@host:path"), "scp form with empty user should be invalid");
	ASSERT(!url_is_valid("a@b@c:path"), "scp form with multiple @ should be invalid");
	PASS();
}

/* ---------------------------------------------------------------
 * version_is_valid unit tests
 * --------------------------------------------------------------- */
TEST(version_is_valid_unit)
{
	ASSERT(version_is_valid("1.0.0"), "1.0.0 should be valid");
	ASSERT(version_is_valid("v1.2"), "v1.2 should be valid");
	ASSERT(version_is_valid("2.0"), "2.0 should be valid");
	ASSERT(version_is_valid("1.0.0-alpha.1+build.5"), "semver pre-release should be valid");
	ASSERT(version_is_valid("*"), "wildcard sentinel should be valid");
	ASSERT(!version_is_valid(nullptr), "nullptr should be invalid");
	ASSERT(!version_is_valid(""), "empty should be invalid");
	/* version_parse() accepts this (it stops after the patch component);
	 * the path validator must not. */
	ASSERT(!version_is_valid("1.0.0/../../x"), "traversal should be invalid");
	ASSERT(!version_is_valid(".."), ".. should be invalid");
	ASSERT(!version_is_valid("."), ". should be invalid");
	ASSERT(!version_is_valid(".hidden"), "leading dot should be invalid");
	ASSERT(!version_is_valid("-x"), "leading dash should be invalid");
	ASSERT(!version_is_valid("1.0.0 x"), "whitespace should be invalid");
	ASSERT(!version_is_valid("1.0.0;rm -rf /"), "shell metacharacters should be invalid");
	ASSERT(!version_is_valid("1.0.0\\..\\x"), "backslash should be invalid");
	ASSERT(!version_is_valid("1.0.0\"x"), "double quote should be invalid");
	PASS();
}

/* ---------------------------------------------------------------
 * manifest — parse/write round-trip preserves inline-table deps
 * --------------------------------------------------------------- */
TEST(manifest_roundtrip_preserves_inline_deps)
{
	char old_cwd[4'096];
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
		if (m2->package.dependencies[i] != nullptr && strncmp(m2->package.dependencies[i], "plain", 5) == 0) {
			found_plain = true;
		}
	}
	ASSERT(found_git, "git dep should survive round-trip");
	ASSERT(found_path, "path dep should survive round-trip");
	ASSERT(found_plain, "plain dep should survive round-trip");

	/* Remove a structured dep: both arrays must be purged */
	ASSERT(manifest_remove_dependency(m2, "mylib"), "remove should find mylib");
	ASSERT(!manifest_remove_dependency(m2, "mylib"), "second remove should fail");
	ASSERT(manifest_write("Coffee.toml", m2) == 0, "manifest_write should succeed after remove");
	manifest_free(m2);

	manifest_t *m3 = manifest_parse("Coffee.toml");
	ASSERT(m3 != nullptr, "re-parse after remove should succeed");
	ASSERT(m3->dependencies.deps_count == 1, "one structured dep should remain");
	bool path_survives = false;
	for (size_t i = 0; i < m3->dependencies.deps_count; i++) {
		if (m3->dependencies.deps[i].name != nullptr && strcmp(m3->dependencies.deps[i].name, "mypath") == 0) {
			path_survives = true;
		}
	}
	ASSERT(path_survives, "mypath should survive the remove");
	/* Flat array: no mylib remnant, plain still present */
	bool mylib_gone = true;
	bool plain_here = false;
	for (size_t i = 0; i < m3->package.dependencies_count; i++) {
		if (m3->package.dependencies[i] == nullptr) {
			continue;
		}
		sds dep_name = dep_parse_name(m3->package.dependencies[i]);
		if (strcmp(dep_name, "mylib") == 0) {
			mylib_gone = false;
		}
		if (strcmp(dep_name, "plain") == 0) {
			plain_here = true;
		}
		sdsfree(dep_name);
	}
	ASSERT(mylib_gone, "no mylib remnant in flat array after remove");
	ASSERT(plain_here, "plain dep should survive the remove");
	manifest_free(m3);

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
	test_remove_tree(tmpdir);
	mkdir(tmpdir, 0755);
	char *old_home = getenv("COFFEE_HOME");
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
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	PASS();
}

/* registry — registry_source_url resolves recipe_url from a local fixture */
TEST(registry_source_url_resolves_recipe_url)
{
	/* Requires curl; skip (pass) when unavailable. */
	{
		char *curl_argv[] = { "curl", "--version", nullptr };
		if (run_command(curl_argv, RUN_CMD_QUIET) != 0) {
			printf("  (curl not available, skipping)\n");
			PASS();
		}
	}

	{
		char *clean_argv[] = { "rm", "-rf", "/tmp/coffee-test-regsrc", nullptr };
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	sds base = sdsnew("/tmp/coffee-test-regsrc");
	mkdir(base, 0755);
	sds reg_dir = sdscatprintf(sdsempty(), "%s/registry", base);
	mkdir(reg_dir, 0755);
	sds recipes = sdscatprintf(sdsempty(), "%s/recipes", reg_dir);
	mkdir(recipes, 0755);
	sds letter = sdscatprintf(sdsempty(), "%s/recipes/a", reg_dir);
	mkdir(letter, 0755);
	sds pkg = sdscatprintf(sdsempty(), "%s/alpha", letter);
	mkdir(pkg, 0755);
	sds   toml = sdscatprintf(sdsempty(), "%s/library.toml", pkg);
	FILE *fp   = fopen(toml, "w");
	ASSERT(fp != nullptr, "fopen recipe library.toml failed");
	fprintf_safe(fp, "version = \"1.0\"\n");
	fprintf_safe(fp, "recipe_url = \"/tmp/coffee-test-regsrc/alpha-repo\"\n");
	fclose(fp);

	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	bool found = false;
	sds  url   = registry_source_url("alpha", &found);
	ASSERT(found, "alpha recipe should be found");
	ASSERT(url != nullptr, "alpha should have a source URL");
	ASSERT(url != nullptr && strcmp(url, "/tmp/coffee-test-regsrc/alpha-repo") == 0,
	       "source URL should match recipe_url");

	bool missing_found = true;
	sds  missing       = registry_source_url("nope", &missing_found);
	ASSERT(!missing_found, "unknown package should not be found");
	ASSERT(missing == nullptr, "unknown package should have no source URL");

	sdsfree(url);
	sdsfree(missing);
	sdsfree(reg_url);
	sdsfree(toml);
	sdsfree(pkg);
	sdsfree(letter);
	sdsfree(recipes);
	sdsfree(reg_dir);
	sdsfree(base);

	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	{
		char *rm_argv[] = { "rm", "-rf", "/tmp/coffee-test-regsrc", nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	PASS();
}

/* registry — recipe URL policy: local paths only for a local registry */
TEST(registry_recipe_url_policy)
{
	const char *old_reg = getenv("COFFEE_REGISTRY_URL");

	/* Remote registry: local recipe URLs rejected, network ones accepted. */
	setenv("COFFEE_REGISTRY_URL", "https://example.com/reg", 1);
	ASSERT(!registry_recipe_url_is_valid("/tmp/foo"), "local path rejected for remote registry");
	ASSERT(!registry_recipe_url_is_valid("./foo"), "relative path rejected for remote registry");
	ASSERT(registry_recipe_url_is_valid("https://github.com/x/y.git"), "https accepted");
	ASSERT(registry_recipe_url_is_valid("git@github.com:x/y.git"), "scp-style accepted");
	ASSERT(!registry_recipe_url_is_valid("http://x/y.git"), "http rejected");

	/* Local registry: local recipe URLs accepted. */
	setenv("COFFEE_REGISTRY_URL", "file:///tmp/reg", 1);
	ASSERT(registry_recipe_url_is_valid("/tmp/foo"), "local path accepted for local registry");

	/* Unset: default remote registry. */
	unsetenv("COFFEE_REGISTRY_URL");
	ASSERT(!registry_recipe_url_is_valid("/tmp/foo"), "local path rejected for default registry");

	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	PASS();
}

/* ---------------------------------------------------------------
 * registry/fetch fixtures — local git repo + recipe
 * --------------------------------------------------------------- */
static void make_local_git_repo(const char *repo_dir, const char *name)
{
	mkdir(repo_dir, 0755);
	assert(chdir(repo_dir) == 0);

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp != nullptr);
	fprintf_safe(fp, "[package]\nname = \"%s\"\nversion = \"1.0.0\"\nedition = \"c23\"\n", name);
	fclose(fp);

	{
		char *init_argv[] = { "git", "init", "-q", nullptr };
		assert(run_command(init_argv, RUN_CMD_QUIET) == 0);
		char *add_argv[] = { "git", "add", "-A", nullptr };
		assert(run_command(add_argv, RUN_CMD_QUIET) == 0);
		char *commit_argv[] = {
			"git", "-c", "user.email=test@test", "-c", "user.name=test", "commit", "-q", "-m", "init", nullptr,
		};
		assert(run_command(commit_argv, RUN_CMD_QUIET) == 0);
	}
}

/* Write a recipe fixture for `name` under reg_dir.  A null recipe_url
 * omits the key entirely. */
static void write_recipe(const char *reg_dir, const char *name, const char *recipe_url)
{
	sds recipes = sdscatprintf(sdsempty(), "%s/recipes", reg_dir);
	mkdir(recipes, 0755);
	sdsfree(recipes);

	char first  = (char)tolower((unsigned char)name[0]);
	sds  letter = sdscatprintf(sdsempty(), "%s/recipes/%c", reg_dir, first);
	mkdir(letter, 0755);
	sds pkg = sdscatprintf(sdsempty(), "%s/%s", letter, name);
	mkdir(pkg, 0755);
	sds   toml = sdscatprintf(sdsempty(), "%s/library.toml", pkg);
	FILE *fp   = fopen(toml, "w");
	assert(fp != nullptr);
	fprintf_safe(fp, "version = \"1.0\"\n");
	if (recipe_url != nullptr) {
		fprintf_safe(fp, "recipe_url = \"%s\"\n", recipe_url);
	}
	fclose(fp);
	sdsfree(toml);
	sdsfree(pkg);
	sdsfree(letter);
}

/* ---------------------------------------------------------------
 * add — a bare package name resolves from the recipes catalog
 * --------------------------------------------------------------- */
TEST(add_bare_name_from_registry)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-registry");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	/* Local registry fixture: a recipe for "alpha" with a recipe_url. */
	sds reg_dir = sdsnew("/tmp/coffee-test-add-registry-dir");
	mkdir(reg_dir, 0755);
	write_recipe(reg_dir, "alpha", "https://example.com/alpha.git");

	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	options opt = {
		.inputs     = (char *[]){ "add", "alpha" },
		.inputs_num = 2,
	};
	ASSERT(handle_add(&opt) == 0, "add with a bare name from the catalog should succeed");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest should parse after add");
	ASSERT(m->dependencies.deps_count == 1, "one structured dep should be written");
	bool found = false;
	if (m->dependencies.deps_count == 1) {
		dependency_t *d = &m->dependencies.deps[0];
		found           = d->name != nullptr && strcmp(d->name, "alpha") == 0 && d->git != nullptr &&
		                  strcmp(d->git, "https://example.com/alpha.git") == 0;
	}
	ASSERT(found, "alpha should carry the recipe_url as its git source");
	manifest_free(m);

	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	sdsfree(reg_url);
	sdsfree(reg_dir);
	{
		char *rm_argv[] = { "rm", "-rf", "/tmp/coffee-test-add-registry-dir", nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

TEST(add_bare_name_unknown_fails)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-registry-unknown");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	/* Empty local registry fixture: the lookup must fail fast. */
	sds reg_dir = sdsnew("/tmp/coffee-test-add-registry-empty");
	mkdir(reg_dir, 0755);

	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	options opt = {
		.inputs     = (char *[]){ "add", "nope" },
		.inputs_num = 2,
	};
	ASSERT(handle_add(&opt) == 1, "unknown package should fail");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest should still parse");
	ASSERT(m->dependencies.deps_count == 0, "no dependency should have been written");
	manifest_free(m);

	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	sdsfree(reg_url);
	sdsfree(reg_dir);
	{
		char *rm_argv[] = { "rm", "-rf", "/tmp/coffee-test-add-registry-empty", nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

TEST(add_bare_name_offline_fails)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-registry-offline");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "add", "alpha" },
		.inputs_num = 2,
		.offline    = true,
	};
	ASSERT(handle_add(&opt) == 1, "offline bare-name add should fail");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest should still parse");
	ASSERT(m->dependencies.deps_count == 0, "no dependency should have been written");
	manifest_free(m);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * add — a recipe that exists but has no usable source URL
 * --------------------------------------------------------------- */
TEST(add_recipe_without_source_fails)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-add-nosource");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	/* Recipe present, but no recipe_url key. */
	sds reg_dir = sdsnew("/tmp/coffee-test-add-nosource-dir");
	mkdir(reg_dir, 0755);
	write_recipe(reg_dir, "nosrc", nullptr);

	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	options opt = {
		.inputs     = (char *[]){ "add", "nosrc" },
		.inputs_num = 2,
	};
	ASSERT(handle_add(&opt) == 1, "recipe without a source URL should fail");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest should still parse");
	ASSERT(m->dependencies.deps_count == 0, "no dependency should have been written");
	manifest_free(m);

	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	sdsfree(reg_url);
	sdsfree(reg_dir);
	{
		char *rm_argv[] = { "rm", "-rf", "/tmp/coffee-test-add-nosource-dir", nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — a direct flat dep materializes from the registry
 * --------------------------------------------------------------- */
TEST(fetch_direct_flat_dep_from_registry)
{
	/* Requires git and curl; skip (pass) when unavailable. */
	{
		char *git_argv[] = { "git", "--version", nullptr };
		if (run_command(git_argv, RUN_CMD_QUIET) != 0) {
			printf("  (git not available, skipping)\n");
			PASS();
		}
	}
	{
		char *curl_argv[] = { "curl", "--version", nullptr };
		if (run_command(curl_argv, RUN_CMD_QUIET) != 0) {
			printf("  (curl not available, skipping)\n");
			PASS();
		}
	}

	{
		char *clean_argv[] = {
			"rm",
			"-rf",
			"/tmp/coffee-test-fetch-direct-repo",
			"/tmp/coffee-test-fetch-direct-proj",
			"/tmp/coffee-test-fetch-direct-home",
			"/tmp/coffee-test-fetch-direct-registry",
			nullptr,
		};
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	/* The registry recipe for "foo" points at this repo. */
	sds repo_dir = sdsnew("/tmp/coffee-test-fetch-direct-repo");
	make_local_git_repo(repo_dir, "foo");

	/* Local registry fixture. */
	sds reg_dir = sdsnew("/tmp/coffee-test-fetch-direct-registry");
	mkdir(reg_dir, 0755);
	write_recipe(reg_dir, "foo", repo_dir);

	/* Sandbox COFFEE_HOME and the registry URL. */
	sds test_home = sdsnew("/tmp/coffee-test-fetch-direct-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);
	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	/* Project with a direct flat dep. */
	sds proj_dir = sdsnew("/tmp/coffee-test-fetch-direct-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nfoo = \"1.0\"\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};

	i64 ret = handle_fetch(&opt);
	ASSERT(ret == 0, "fetch with direct flat dep should succeed");
	ASSERT(access("deps/foo", F_OK) == 0, "foo materialized");

	lockfile_t *lf = lockfile_parse("Coffee.lock");
	ASSERT(lf != nullptr, "Coffee.lock should parse");
	ASSERT(lf->deps_count == 1, "lockfile has one dep");
	ASSERT(lf->deps[0].name != nullptr && strcmp(lf->deps[0].name, "foo") == 0, "lockfile dep is foo");
	ASSERT(lf->deps[0].commit != nullptr, "foo commit recorded");
	lockfile_free(lf);

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	{
		char *rm_argv[] = { "rm", "-rf", repo_dir, proj_dir, test_home, reg_dir, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(reg_url);
	sdsfree(reg_dir);
	sdsfree(repo_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — a recipe whose recipe_url is not a valid remote URL fails
 * --------------------------------------------------------------- */
TEST(fetch_registry_dep_rejects_unsafe_recipe_url)
{
	/* Requires curl; skip (pass) when unavailable. */
	{
		char *curl_argv[] = { "curl", "--version", nullptr };
		if (run_command(curl_argv, RUN_CMD_QUIET) != 0) {
			printf("  (curl not available, skipping)\n");
			PASS();
		}
	}

	{
		char *clean_argv[] = {
			"rm",
			"-rf",
			"/tmp/coffee-test-fetch-badurl-proj",
			"/tmp/coffee-test-fetch-badurl-home",
			"/tmp/coffee-test-fetch-badurl-registry",
			nullptr,
		};
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	/* Local registry fixture: the recipe exists but its recipe_url is
	 * http, which is not on the allow-list. */
	sds reg_dir = sdsnew("/tmp/coffee-test-fetch-badurl-registry");
	mkdir(reg_dir, 0755);
	write_recipe(reg_dir, "foo", "http://127.0.0.1/foo.git");

	sds test_home = sdsnew("/tmp/coffee-test-fetch-badurl-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);
	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-fetch-badurl-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nfoo = \"1.0\"\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};

	i64 ret = handle_fetch(&opt);
	ASSERT(ret == 1, "unsafe recipe_url should fail the fetch");
	ASSERT(lockfile_parse("Coffee.lock") == nullptr, "no Coffee.lock written");

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	{
		char *rm_argv[] = { "rm", "-rf", proj_dir, test_home, reg_dir, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(reg_url);
	sdsfree(reg_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — a recipe with no recipe_url fails cleanly
 * --------------------------------------------------------------- */
TEST(fetch_registry_dep_missing_recipe_url)
{
	/* Requires curl; skip (pass) when unavailable. */
	{
		char *curl_argv[] = { "curl", "--version", nullptr };
		if (run_command(curl_argv, RUN_CMD_QUIET) != 0) {
			printf("  (curl not available, skipping)\n");
			PASS();
		}
	}

	{
		char *clean_argv[] = {
			"rm",
			"-rf",
			"/tmp/coffee-test-fetch-nourl-proj",
			"/tmp/coffee-test-fetch-nourl-home",
			"/tmp/coffee-test-fetch-nourl-registry",
			nullptr,
		};
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	/* Local registry fixture: the recipe exists but has no recipe_url. */
	sds reg_dir = sdsnew("/tmp/coffee-test-fetch-nourl-registry");
	mkdir(reg_dir, 0755);
	write_recipe(reg_dir, "foo", nullptr);

	sds test_home = sdsnew("/tmp/coffee-test-fetch-nourl-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);
	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-fetch-nourl-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nfoo = \"1.0\"\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};

	i64 ret = handle_fetch(&opt);
	ASSERT(ret == 1, "missing recipe_url should fail the fetch");
	ASSERT(lockfile_parse("Coffee.lock") == nullptr, "no Coffee.lock written");

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	{
		char *rm_argv[] = { "rm", "-rf", proj_dir, test_home, reg_dir, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(reg_url);
	sdsfree(reg_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
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
 * safe_fsync_dir: best-effort and total for every path shape
 * --------------------------------------------------------------- */
TEST(safe_fsync_dir_handles_any_path_shape)
{
	/* A bare relative name (no slash) must fsync ".", not be skipped. */
	char bare[]   = "Coffee.lock";
	char rooted[] = "/coffee-nonexistent-dir";
	char nested[] = "some/nested/dir/Coffee.lock";
	char slashy[] = "some/nested/";
	char empty[]  = "";

	safe_fsync_dir(bare);
	safe_fsync_dir(rooted);
	safe_fsync_dir(nested);
	safe_fsync_dir(slashy);
	safe_fsync_dir(empty);

	/* The path is borrowed: the helper must not write through it. */
	ASSERT(strcmp(bare, "Coffee.lock") == 0, "borrowed path must be left alone");
	ASSERT(strcmp(rooted, "/coffee-nonexistent-dir") == 0, "borrowed path must be left alone");
	ASSERT(strcmp(nested, "some/nested/dir/Coffee.lock") == 0, "borrowed path must be left alone");
	ASSERT(strcmp(slashy, "some/nested/") == 0, "borrowed path must be left alone");
	ASSERT(strcmp(empty, "") == 0, "borrowed path must be left alone");

	/* Exercise the branch where the directory really is opened. */
	sds dir = sdsnew("/tmp/coffee-fsync-dir-test");
	if (mkdir(dir, 0700) == 0) {
		sds file = sdscatprintf(sdsempty(), "%s/Coffee.toml", dir);
		safe_fsync_dir(file);
		sdsfree(file);
		rmdir(dir);
	}
	sdsfree(dir);
	PASS();
}

/* ---------------------------------------------------------------
 * The safe-building-block headers must not re-export banned footguns
 * --------------------------------------------------------------- */

/* Read a whole file into an sds; an unreadable file yields an empty sds. */
static sds read_file(const char *path)
{
	sds   body = sdsempty();
	FILE *fp   = fopen(path, "r");
	if (fp == nullptr) {
		return body;
	}

	char   buf[4'096];
	size_t got;
	do {
		got  = fread(buf, 1, sizeof(buf), fp);
		body = sdscatlen(body, buf, got);
	} while (got > 0);
	fclose(fp);
	return body;
}

/* Resolve a path relative to this file's own compile-time directory, so the
 * check does not depend on the current directory.  If __FILE__ carries no
 * directory component, the bare relative path is used. */
static sds header_path(const char *relative)
{
	sds   path  = sdsnew(__FILE__);
	char *slash = strrchr(path, '/');
	if (slash != nullptr) {
		sdsrange(path, 0, (ssize_t)(slash - path));
	} else {
		sdsclear(path);
	}
	path = sdscat(path, relative);
	return path;
}

TEST(safe_header_exports_no_banned_wrappers)
{
	static const char *const banned[] = {
		"safe_snprintf", "safe_fprintf", "safe_printf", "safe_memcpy", "safe_memset",
		"safe_sprintf",  "safe_strcpy",  "safe_scanf",  "safe_system", "safe_popen",
	};
	static const char *const guarded[] = {
		"../include/safe.h",
		"../src/strings.h",
	};

	i64 leaks = 0;
	i64 kept  = 0;

	for (size_t h = 0; h < sizeof(guarded) / sizeof(guarded[0]); h++) {
		sds path = header_path(guarded[h]);
		sds body = read_file(path);
		if (sdslen(body) == 0) {
			printf("  could not read %s\n", path);
			sdsfree(body);
			sdsfree(path);
			FAIL("a guarded header could not be read");
		}

		for (size_t i = 0; i < sizeof(banned) / sizeof(banned[0]); i++) {
			if (strstr(body, banned[i]) != nullptr) {
				printf("  %s re-exports banned wrapper: %s\n", guarded[h], banned[i]);
				leaks++;
			}
		}

		/* The wrappers that are actually used must stay. */
		if (strstr(body, "safe_malloc") != nullptr) {
			kept++;
		}
		if (strstr(body, "safe_fopen") != nullptr) {
			kept++;
		}
		if (strstr(body, "safe_strtol") != nullptr) {
			kept++;
		}

		sdsfree(body);
		sdsfree(path);
	}

	if (leaks > 0) {
		FAIL("a guarded header must not export banned wrappers");
	}
	ASSERT(kept == 3, "the live safe_* wrappers should stay");
	PASS();
}

/* ---------------------------------------------------------------
 * manifest — traversal name rejection at parse time
 * --------------------------------------------------------------- */
TEST(manifest_rejects_traversal_name)
{
	char old_cwd[4'096];
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

/* ---------------------------------------------------------------
 * manifest_write — invalid bare-key remnants are not re-emitted
 * --------------------------------------------------------------- */
TEST(manifest_write_drops_invalid_flat_remnant)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-manifest-write-remnant");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "[dependencies]\n");
	fprintf_safe(fp, "normal = { path = \".\" }\n");
	fprintf_safe(fp, "\"../evil\" = { path = \".\" }\n");
	fprintf_safe(fp, "\"../evil2\" = \"1.0\"\n");
	fprintf_safe(fp, "\"a b\" = { path = \".\" }\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest_parse should succeed");
	ASSERT(m->dependencies.deps_count == 1, "only the valid structured dep should parse");
	ASSERT(manifest_write("Coffee.toml", m) == 0, "manifest_write should succeed");
	manifest_free(m);

	FILE *rf = fopen("Coffee.toml", "r");
	ASSERT(rf != nullptr, "fopen for read failed");
	char   buf[4'096];
	size_t n = fread(buf, 1, sizeof(buf) - 1, rf);
	buf[n]   = '\0';
	fclose(rf);
	ASSERT(strstr(buf, "../evil") == nullptr, "invalid remnant must not be written back");
	ASSERT(strstr(buf, "../evil2") == nullptr, "invalid '='-bearing remnant must not be written back");
	ASSERT(strstr(buf, "a b") == nullptr, "non-bare-key remnant must not be written back");

	manifest_t *re = manifest_parse("Coffee.toml");
	ASSERT(re != nullptr, "rewritten manifest must still parse");
	ASSERT(re->dependencies.deps_count == 1, "only the valid dep should remain");
	ASSERT(re->dependencies.deps[0].name != nullptr, "valid dep should have name");
	ASSERT(strcmp(re->dependencies.deps[0].name, "normal") == 0, "remaining dep should be 'normal'");
	manifest_free(re);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);

	PASS();
}

/* ---------------------------------------------------------------
 * add/remove — exact-name matching (no prefix match)
 * --------------------------------------------------------------- */
TEST(add_remove_exact_name_match)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-exact-name");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"test\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "[dependencies]\nfoo = \"1.0\"\nfoobar = \"2.0\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest_parse should succeed");
	ASSERT(manifest_remove_dependency(m, "foo"), "remove foo should succeed");
	ASSERT(m->package.dependencies_count == 1, "one flat dep should remain");
	ASSERT(m->package.dependencies[0] != nullptr, "remaining dep present");
	sds remaining = dep_parse_name(m->package.dependencies[0]);
	ASSERT(strcmp(remaining, "foobar") == 0, "foobar should survive, foo removed");
	sdsfree(remaining);
	manifest_free(m);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * install — unsafe binary target names rejected
 * --------------------------------------------------------------- */
TEST(install_rejects_unsafe_bin_name)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-unsafe-bin");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"evil\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "[[bin]]\nname = \"../evil\"\nsrc = [\"main.c\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "manifest_parse should succeed");
	ASSERT(m->bin_count == 1, "one binary target should parse");
	if (m->bin_count > 0) {
		ASSERT(!dep_name_is_valid(m->bin[0].name), "traversal bin name should be invalid");
	}
	manifest_free(m);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(tmpdir);
	sdsfree(tmpdir);
	PASS();
}

/* ---------------------------------------------------------------
 * install — traversal version from cloned library.toml rejected
 * --------------------------------------------------------------- */
TEST(install_rejects_traversal_version)
{
	/* Requires git; skip (pass) when unavailable. */
	{
		char *git_argv[] = { "git", "--version", nullptr };
		if (run_command(git_argv, RUN_CMD_QUIET) != 0) {
			printf("  (git not available, skipping)\n");
			PASS();
		}
	}

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	/* Build a malicious repo whose version escapes the deps dir */
	sds repo_dir = sdsnew("/tmp/coffee-test-install-evil-repo");
	mkdir(repo_dir, 0755);
	ASSERT(chdir(repo_dir) == 0, "chdir to repo failed");

	FILE *fp = fopen("library.toml", "w");
	ASSERT(fp != nullptr, "fopen library.toml failed");
	fprintf_safe(fp, "[package]\nname = \"evil\"\nversion = \"../../pwned\"\n");
	fclose(fp);

	mkdir("src", 0755);
	fp = fopen("src/main.c", "w");
	ASSERT(fp != nullptr, "fopen main.c failed");
	fprintf_safe(fp, "int main(void) { return 0; }\n");
	fclose(fp);

	{
		char *init_argv[] = { "git", "init", "-q", nullptr };
		ASSERT(run_command(init_argv, RUN_CMD_QUIET) == 0, "git init failed");
		char *add_argv[] = { "git", "add", "-A", nullptr };
		ASSERT(run_command(add_argv, RUN_CMD_QUIET) == 0, "git add failed");
		char *commit_argv[] = {
			"git", "-c", "user.email=test@test", "-c", "user.name=test", "commit", "-q", "-m", "init", nullptr,
		};
		ASSERT(run_command(commit_argv, RUN_CMD_QUIET) == 0, "git commit failed");
	}

	/* Sandbox COFFEE_HOME and a project dir */
	sds test_home = sdsnew("/tmp/coffee-test-install-evil-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-install-evil-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "install", "evil" },
		.inputs_num = 2,
		.git        = repo_dir,
	};
	i64 ret = handle_install(&opt);

	ASSERT(ret == 1, "install should reject traversal version");

	sds pwned = sdscatprintf(sdsempty(), "%s/pwned", test_home);
	ASSERT(access(pwned, F_OK) != 0, "no directory should be created outside deps");
	sdsfree(pwned);

	sds evil_dep = sdscatprintf(sdsempty(), "%s/deps/evil", test_home);
	ASSERT(access(evil_dep, F_OK) != 0, "malicious clone should be cleaned up");
	sdsfree(evil_dep);

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	{
		char *rm_argv[] = { "rm", "-rf", repo_dir, proj_dir, test_home, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(repo_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
	PASS();
}

/* ---------------------------------------------------------------
 * install — a regular file at deps/<name> must not block the symlink
 * --------------------------------------------------------------- */
TEST(install_replaces_regular_file)
{
	{
		char *git_argv[] = { "git", "--version", nullptr };
		if (run_command(git_argv, RUN_CMD_QUIET) != 0) {
			printf("  (git not available, skipping)\n");
			PASS();
		}
	}

	/* Drop leftovers from an aborted previous run so the fixture is
	 * re-created from scratch (fixed paths, like the sibling fetch
	 * tests). */
	{
		char *clean_argv[] = {
			"rm",
			"-rf",
			"/tmp/coffee-test-install-replace-repo",
			"/tmp/coffee-test-install-replace-proj",
			"/tmp/coffee-test-install-replace-home",
			nullptr,
		};
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	/* Source repo with a valid library.toml. */
	sds repo_dir = sdsnew("/tmp/coffee-test-install-replace-repo");
	mkdir(repo_dir, 0755);
	ASSERT(chdir(repo_dir) == 0, "chdir to repo failed");

	FILE *fp = fopen("library.toml", "w");
	ASSERT(fp != nullptr, "fopen library.toml failed");
	fprintf_safe(fp, "[package]\nname = \"goodlib\"\nversion = \"1.0.0\"\n");
	fclose(fp);

	mkdir("src", 0755);
	fp = fopen("src/main.c", "w");
	ASSERT(fp != nullptr, "fopen main.c failed");
	fprintf_safe(fp, "int main(void) { return 0; }\n");
	fclose(fp);

	{
		char *init_argv[] = { "git", "init", "-q", nullptr };
		ASSERT(run_command(init_argv, RUN_CMD_QUIET) == 0, "git init failed");
		char *add_argv[] = { "git", "add", "-A", nullptr };
		ASSERT(run_command(add_argv, RUN_CMD_QUIET) == 0, "git add failed");
		char *commit_argv[] = {
			"git", "-c", "user.email=test@test", "-c", "user.name=test", "commit", "-q", "-m", "init", nullptr,
		};
		ASSERT(run_command(commit_argv, RUN_CMD_QUIET) == 0, "git commit failed");
	}

	/* Sandbox COFFEE_HOME and a project dir. */
	sds test_home = sdsnew("/tmp/coffee-test-install-replace-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-install-replace-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	/* A regular file at deps/goodlib must not block the symlink. */
	mkdir("deps", 0755);
	fp = fopen("deps/goodlib", "w");
	ASSERT(fp != nullptr, "fopen blocking file failed");
	fprintf_safe(fp, "stale\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "install", "goodlib" },
		.inputs_num = 2,
		.git        = repo_dir,
	};
	i64 ret = handle_install(&opt);

	ASSERT(ret == 0, "install should succeed");
	struct stat st;
	ASSERT(lstat("deps/goodlib", &st) == 0, "deps/goodlib should exist");
	ASSERT(S_ISLNK(st.st_mode), "deps/goodlib should now be a symlink");

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	{
		char *rm_argv[] = { "rm", "-rf", repo_dir, proj_dir, test_home, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(repo_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
	PASS();
}

/* ---------------------------------------------------------------
 * toolcheck — probing limited to build/fetch commands
 * --------------------------------------------------------------- */

/* Create /tmp/coffee-toolcheck-stub-<tool>-<pid>/<tool>, a fake tool that
 * exits 0, so a test can put exactly one tool on PATH.  Returns the
 * directory (free with toolcheck_stub_free) or nullptr. */
static sds toolcheck_stub_dir(const char *tool)
{
	sds   dir          = sdscatprintf(sdsempty(), "/tmp/coffee-toolcheck-stub-%s-%d", tool, (int)getpid());
	char *mkdir_argv[] = { "mkdir", "-p", dir, nullptr };
	if (run_command(mkdir_argv, RUN_CMD_QUIET) != 0) {
		sdsfree(dir);
		return nullptr;
	}

	sds   path = sdscatprintf(sdsempty(), "%s/%s", dir, tool);
	FILE *fp   = fopen(path, "w");
	if (fp == nullptr) {
		sdsfree(path);
		sdsfree(dir);
		return nullptr;
	}
	fprintf_safe(fp, "#!/bin/sh\nexit 0\n");
	fclose(fp);
	chmod(path, 0755);
	sdsfree(path);
	return dir;
}

static void toolcheck_stub_free(sds dir)
{
	if (dir == nullptr) {
		return;
	}
	char *rm_argv[] = { "rm", "-rf", dir, nullptr };
	run_command(rm_argv, RUN_CMD_QUIET);
	sdsfree(dir);
}

static void toolcheck_restore_path(char *old_path)
{
	if (old_path != nullptr) {
		setenv("PATH", old_path, 1);
	} else {
		unsetenv("PATH");
	}
}

TEST(toolcheck_skips_non_build_commands)
{
	/* With an empty PATH no tool can be found: commands that need clang or
	 * git must report failure while metadata commands must still pass. */
	char *old_path = getenv("PATH");
	setenv("PATH", "/nonexistent", 1);

	ASSERT(toolcheck_run("config") == 0, "config should not probe tools");
	ASSERT(toolcheck_run("search") == 0, "search should not probe tools");
	ASSERT(toolcheck_run("fetch") != 0, "fetch should probe tools and fail without them");
	ASSERT(toolcheck_run("update") != 0, "update should probe tools and fail without them");
	ASSERT(toolcheck_run("vendor") != 0, "vendor should probe tools and fail without them");
	ASSERT(toolcheck_run("outdated") != 0, "outdated should probe tools and fail without them");
	ASSERT(toolcheck_run("generate-lockfile") != 0, "generate-lockfile should probe tools");
	ASSERT(toolcheck_run("install-update") != 0, "install-update should probe tools");
	ASSERT(toolcheck_run("doc") == 0, "doc should not probe tools (self-checks doxygen)");
	ASSERT(toolcheck_run("package") == 0, "package should not probe tools (self-checks tar)");

	toolcheck_restore_path(old_path);
	PASS();
}

/* The fetch family must work on a machine without clang. */
TEST(toolcheck_fetch_needs_only_git)
{
	sds stub = toolcheck_stub_dir("git");
	ASSERT(stub != nullptr, "could not create stub git");

	char *old_path = getenv("PATH");
	setenv("PATH", stub, 1);

	ASSERT(toolcheck_run("fetch") == 0, "fetch needs git only");
	ASSERT(toolcheck_run("update") == 0, "update needs git only");
	ASSERT(toolcheck_run("vendor") == 0, "vendor needs git only");
	ASSERT(toolcheck_run("outdated") == 0, "outdated needs git only");
	ASSERT(toolcheck_run("generate-lockfile") == 0, "generate-lockfile needs git only");
	ASSERT(toolcheck_run("install-update") == 0, "install-update needs git only");

	ASSERT(toolcheck_run("build") != 0, "build still needs clang");
	ASSERT(toolcheck_run("b") != 0, "b still needs clang");
	ASSERT(toolcheck_run("compile") != 0, "compile still needs clang");
	ASSERT(toolcheck_run("check") != 0, "check still needs clang");
	ASSERT(toolcheck_run("c") != 0, "c still needs clang");
	ASSERT(toolcheck_run("test") != 0, "test still needs clang");
	ASSERT(toolcheck_run("bench") != 0, "bench still needs clang");
	ASSERT(toolcheck_run("lint") != 0, "lint still needs clang");
	ASSERT(toolcheck_run("fix") != 0, "fix still needs clang");
	ASSERT(toolcheck_run("run") != 0, "run still needs clang");
	ASSERT(toolcheck_run("install") != 0, "install needs clang too");

	toolcheck_restore_path(old_path);
	toolcheck_stub_free(stub);
	PASS();
}

/* Compiling commands must not start passing when only git is absent. */
TEST(toolcheck_build_needs_clang)
{
	sds stub = toolcheck_stub_dir("clang");
	ASSERT(stub != nullptr, "could not create stub clang");

	char *old_path = getenv("PATH");
	setenv("PATH", stub, 1);

	ASSERT(toolcheck_run("build") == 0, "build needs clang only");
	ASSERT(toolcheck_run("b") == 0, "b needs clang only");
	ASSERT(toolcheck_run("compile") == 0, "compile needs clang only");
	ASSERT(toolcheck_run("check") == 0, "check needs clang only");
	ASSERT(toolcheck_run("c") == 0, "c needs clang only");
	ASSERT(toolcheck_run("test") == 0, "test needs clang only");
	ASSERT(toolcheck_run("bench") == 0, "bench needs clang only");
	ASSERT(toolcheck_run("lint") == 0, "lint needs clang only");
	ASSERT(toolcheck_run("fix") == 0, "fix needs clang only");
	ASSERT(toolcheck_run("run") == 0, "run needs clang only");

	ASSERT(toolcheck_run("fetch") != 0, "fetch needs git");
	ASSERT(toolcheck_run("update") != 0, "update needs git");
	ASSERT(toolcheck_run("vendor") != 0, "vendor needs git");
	ASSERT(toolcheck_run("outdated") != 0, "outdated needs git");
	ASSERT(toolcheck_run("generate-lockfile") != 0, "generate-lockfile needs git");
	ASSERT(toolcheck_run("install-update") != 0, "install-update needs git");
	ASSERT(toolcheck_run("install") != 0, "install needs git too");

	toolcheck_restore_path(old_path);
	toolcheck_stub_free(stub);
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — transitive deps must not corrupt Coffee.lock
 * --------------------------------------------------------------- */
TEST(fetch_transitive_dep_no_lockfile_corruption)
{
	/* Requires git; skip (pass) when unavailable. */
	{
		char *git_argv[] = { "git", "--version", nullptr };
		if (run_command(git_argv, RUN_CMD_QUIET) != 0) {
			printf("  (git not available, skipping)\n");
			PASS();
		}
	}

	/* Drop leftovers from an aborted previous run so the fixture is
	 * re-created from scratch. */
	{
		char *clean_argv[] = {
			"rm",
			"-rf",
			"/tmp/coffee-test-fetch-transitive-repo",
			"/tmp/coffee-test-fetch-transitive-proj",
			"/tmp/coffee-test-fetch-transitive-home",
			"/tmp/coffee-test-fetch-transitive-nested",
			"/tmp/coffee-test-fetch-transitive-registry",
			nullptr,
		};
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	/* Nested repo: the registry recipe for "nested" points here. */
	sds nested_dir = sdsnew("/tmp/coffee-test-fetch-transitive-nested");
	mkdir(nested_dir, 0755);
	ASSERT(chdir(nested_dir) == 0, "chdir to nested failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen nested Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"nested\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	{
		char *init_argv[] = { "git", "init", "-q", nullptr };
		ASSERT(run_command(init_argv, RUN_CMD_QUIET) == 0, "git init nested failed");
		char *add_argv[] = { "git", "add", "-A", nullptr };
		ASSERT(run_command(add_argv, RUN_CMD_QUIET) == 0, "git add nested failed");
		char *commit_argv[] = {
			"git", "-c", "user.email=test@test", "-c", "user.name=test", "commit", "-q", "-m", "init", nullptr,
		};
		ASSERT(run_command(commit_argv, RUN_CMD_QUIET) == 0, "git commit nested failed");
	}

	/* Local registry fixture: a recipe for "nested" whose recipe_url is
	 * the nested repo above. */
	sds reg_dir = sdsnew("/tmp/coffee-test-fetch-transitive-registry");
	mkdir(reg_dir, 0755);
	sds reg_recipes = sdscatprintf(sdsempty(), "%s/recipes", reg_dir);
	mkdir(reg_recipes, 0755);
	sds reg_letter = sdscatprintf(sdsempty(), "%s/recipes/n", reg_dir);
	mkdir(reg_letter, 0755);
	sds reg_pkg = sdscatprintf(sdsempty(), "%s/nested", reg_letter);
	mkdir(reg_pkg, 0755);
	sds reg_toml = sdscatprintf(sdsempty(), "%s/library.toml", reg_pkg);
	fp           = fopen(reg_toml, "w");
	ASSERT(fp != nullptr, "fopen recipe library.toml failed");
	fprintf_safe(fp, "version = \"1.0\"\n");
	fprintf_safe(fp, "recipe_url = \"%s\"\n", nested_dir);
	fclose(fp);

	/* Parent repo: a git dep that itself declares a nested dependency. */
	sds repo_dir = sdsnew("/tmp/coffee-test-fetch-transitive-repo");
	mkdir(repo_dir, 0755);
	ASSERT(chdir(repo_dir) == 0, "chdir to repo failed");

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen repo Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"parent\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nnested = \"1.0\"\n");
	fclose(fp);

	mkdir("src", 0755);
	fp = fopen("src/main.c", "w");
	ASSERT(fp != nullptr, "fopen repo main.c failed");
	fprintf_safe(fp, "int main(void) { return 0; }\n");
	fclose(fp);

	{
		char *init_argv[] = { "git", "init", "-q", nullptr };
		ASSERT(run_command(init_argv, RUN_CMD_QUIET) == 0, "git init failed");
		char *add_argv[] = { "git", "add", "-A", nullptr };
		ASSERT(run_command(add_argv, RUN_CMD_QUIET) == 0, "git add failed");
		char *commit_argv[] = {
			"git", "-c", "user.email=test@test", "-c", "user.name=test", "commit", "-q", "-m", "init", nullptr,
		};
		ASSERT(run_command(commit_argv, RUN_CMD_QUIET) == 0, "git commit failed");
	}

	/* Sandbox COFFEE_HOME, the registry URL, and a project dir */
	sds test_home = sdsnew("/tmp/coffee-test-fetch-transitive-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);
	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-fetch-transitive-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nparent = { git = \"%s\" }\n", repo_dir);
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};

	i64 ret1 = handle_fetch(&opt);
	ASSERT(ret1 == 0, "first fetch should succeed");

	/* The transitive flat dep "nested" is materialized from the registry. */
	ASSERT(access("deps/nested", F_OK) == 0, "nested materialized");

	lockfile_t *lf1 = lockfile_parse("Coffee.lock");
	ASSERT(lf1 != nullptr, "first Coffee.lock should parse");
	ASSERT(lf1->deps_count == 2, "first lockfile should have both deps");
	bool found_parent = false, found_nested = false;
	for (size_t i = 0; i < lf1->deps_count; i++) {
		if (strcmp(lf1->deps[i].name, "parent") == 0) {
			found_parent = true;
			ASSERT(lf1->deps[i].commit != nullptr, "parent commit recorded");
		}
		if (strcmp(lf1->deps[i].name, "nested") == 0) {
			found_nested = true;
			ASSERT(lf1->deps[i].commit != nullptr, "nested commit recorded");
		}
	}
	ASSERT(found_parent && found_nested, "both deps in first lockfile");
	lockfile_free(lf1);

	i64 ret2 = handle_fetch(&opt);
	ASSERT(ret2 == 0, "second fetch should succeed");

	lockfile_t *lf2 = lockfile_parse("Coffee.lock");
	ASSERT(lf2 != nullptr, "second Coffee.lock should parse");
	ASSERT(lf2->deps_count == 2, "second lockfile should still have both deps");
	lockfile_free(lf2);

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	{
		char *rm_argv[] = { "rm", "-rf", repo_dir, proj_dir, test_home, reg_dir, nested_dir, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(reg_url);
	sdsfree(reg_toml);
	sdsfree(reg_pkg);
	sdsfree(reg_letter);
	sdsfree(reg_recipes);
	sdsfree(reg_dir);
	sdsfree(repo_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
	sdsfree(nested_dir);
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — a root flat dep not in the registry fails
 * --------------------------------------------------------------- */
TEST(fetch_root_flat_dep_not_in_registry_fails)
{
	{
		char *clean_argv[] = {
			"rm",
			"-rf",
			"/tmp/coffee-test-fetch-flat-proj",
			"/tmp/coffee-test-fetch-flat-home",
			"/tmp/coffee-test-fetch-flat-registry",
			nullptr,
		};
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	sds test_home = sdsnew("/tmp/coffee-test-fetch-flat-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);

	/* Empty registry fixture: the flat dep is not in it, so the lookup
	 * fails fast instead of hitting the network. */
	sds reg_dir = sdsnew("/tmp/coffee-test-fetch-flat-registry");
	mkdir(reg_dir, 0755);
	const char *old_reg = getenv("COFFEE_REGISTRY_URL");
	sds         reg_url = sdscatprintf(sdsempty(), "file://%s", reg_dir);
	setenv("COFFEE_REGISTRY_URL", reg_url, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-fetch-flat-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nfoo = \"1.0\"\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};

	i64 ret = handle_fetch(&opt);
	ASSERT(ret == 1, "root flat dep with no source should fail");

	lockfile_t *lf = lockfile_parse("Coffee.lock");
	ASSERT(lf == nullptr, "no Coffee.lock written when the fetch failed");

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	if (old_reg != nullptr) {
		setenv("COFFEE_REGISTRY_URL", old_reg, 1);
	} else {
		unsetenv("COFFEE_REGISTRY_URL");
	}
	{
		char *rm_argv[] = { "rm", "-rf", proj_dir, test_home, reg_dir, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(reg_url);
	sdsfree(reg_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
	PASS();
}

/* ---------------------------------------------------------------
 * fetch — a failed symlink must be reported and fail the dep
 * --------------------------------------------------------------- */
TEST(fetch_symlink_failure_fails)
{
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	{
		char *clean_argv[] = {
			"rm", "-rf", "/tmp/coffee-test-fetch-symlink-proj", "/tmp/coffee-test-fetch-symlink-home", nullptr,
		};
		run_command(clean_argv, RUN_CMD_QUIET);
	}

	sds test_home = sdsnew("/tmp/coffee-test-fetch-symlink-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-fetch-symlink-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");

	mkdir("lib", 0755);

	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nmylib = { path = \"./lib\" }\n");
	fclose(fp);

	/* A regular file at 'deps' makes symlink("deps/mylib", ...) fail with ENOTDIR. */
	fp = fopen("deps", "w");
	ASSERT(fp != nullptr, "fopen deps blocker file failed");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};

	i64 ret = handle_fetch(&opt);
	ASSERT(ret == 1, "fetch should fail when symlink cannot be created");

	lockfile_t *lf = lockfile_parse("Coffee.lock");
	ASSERT(lf == nullptr, "no Coffee.lock written when the fetch failed");

	/* Cleanup */
	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	{
		char *rm_argv[] = { "rm", "-rf", proj_dir, test_home, nullptr };
		run_command(rm_argv, RUN_CMD_QUIET);
	}
	sdsfree(proj_dir);
	sdsfree(test_home);
	PASS();
}

/* A transitive dep with an invalid URL fails the fetch, leaves no lockfile,
 * and does not loop forever. */
TEST(fetch_transitive_invalid_url_fails)
{
	{
		char *git_argv[] = { "git", "--version", nullptr };
		if (run_command(git_argv, RUN_CMD_QUIET) != 0) {
			printf("  (git not available, skipping)\n");
			PASS();
		}
	}
	char *clean_argv[] = {
		"rm", "-rf", "/tmp/coffee-test-ftr-repo", "/tmp/coffee-test-ftr-proj", "/tmp/coffee-test-ftr-home", nullptr,
	};
	run_command(clean_argv, RUN_CMD_QUIET);

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");

	sds repo_dir = sdsnew("/tmp/coffee-test-ftr-repo");
	mkdir(repo_dir, 0755);
	ASSERT(chdir(repo_dir) == 0, "chdir to repo failed");
	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen repo Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"parent\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	/* 'http' is deliberately not an allowed scheme, so this must fail. */
	fprintf_safe(fp, "\n[dependencies]\nnested = { git = \"http://127.0.0.1/nested.git\" }\n");
	fclose(fp);
	{
		char *init_argv[] = { "git", "init", "-q", nullptr };
		ASSERT(run_command(init_argv, RUN_CMD_QUIET) == 0, "git init failed");
		char *add_argv[] = { "git", "add", "-A", nullptr };
		ASSERT(run_command(add_argv, RUN_CMD_QUIET) == 0, "git add failed");
		char *commit_argv[] = {
			"git", "-c", "user.email=test@test", "-c", "user.name=test", "commit", "-q", "-m", "init", nullptr,
		};
		ASSERT(run_command(commit_argv, RUN_CMD_QUIET) == 0, "git commit failed");
	}

	sds test_home = sdsnew("/tmp/coffee-test-ftr-home");
	mkdir(test_home, 0755);
	const char *old_home = getenv("COFFEE_HOME");
	setenv("COFFEE_HOME", test_home, 1);

	sds proj_dir = sdsnew("/tmp/coffee-test-ftr-proj");
	mkdir(proj_dir, 0755);
	ASSERT(chdir(proj_dir) == 0, "chdir to project failed");
	fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "fopen project Coffee.toml failed");
	fprintf_safe(fp, "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fprintf_safe(fp, "\n[dependencies]\nparent = { git = \"%s\" }\n", repo_dir);
	fclose(fp);

	options opt = { .inputs = (char *[]){ "fetch" }, .inputs_num = 1 };
	i64     ret = handle_fetch(&opt);
	ASSERT(ret == 1, "invalid transitive URL should fail the fetch");
	ASSERT(lockfile_parse("Coffee.lock") == nullptr, "no lockfile after a failed fetch");

	chdir(old_cwd);
	if (old_home != nullptr) {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	run_command(clean_argv, RUN_CMD_QUIET);
	sdsfree(repo_dir);
	sdsfree(proj_dir);
	sdsfree(test_home);
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
	TEST_REGISTER(version_is_valid_unit);
	TEST_REGISTER(manifest_roundtrip_preserves_inline_deps);
	TEST_REGISTER(registry_search_parses_mock_index);
	TEST_REGISTER(registry_source_url_resolves_recipe_url);
	TEST_REGISTER(registry_recipe_url_policy);
	TEST_REGISTER(fetch_rejects_dotdot_dep);
	TEST_REGISTER(fetch_rejects_slash_dep);
	TEST_REGISTER(add_rejects_injection_inputs);
	TEST_REGISTER(add_accepts_valid_path_dep);
	TEST_REGISTER(add_ignores_unemitted_values);
	TEST_REGISTER(add_escapes_emitted_values);
	TEST_REGISTER(add_bare_name_from_registry);
	TEST_REGISTER(add_bare_name_unknown_fails);
	TEST_REGISTER(add_bare_name_offline_fails);
	TEST_REGISTER(add_recipe_without_source_fails);
	TEST_REGISTER(manifest_rejects_traversal_name);
	TEST_REGISTER(manifest_write_drops_invalid_flat_remnant);
	TEST_REGISTER(add_remove_exact_name_match);
	TEST_REGISTER(install_rejects_unsafe_bin_name);
	TEST_REGISTER(install_rejects_traversal_version);
	TEST_REGISTER(install_replaces_regular_file);
	TEST_REGISTER(fetch_transitive_dep_no_lockfile_corruption);
	TEST_REGISTER(fetch_transitive_invalid_url_fails);
	TEST_REGISTER(fetch_root_flat_dep_not_in_registry_fails);
	TEST_REGISTER(fetch_symlink_failure_fails);
	TEST_REGISTER(fetch_direct_flat_dep_from_registry);
	TEST_REGISTER(fetch_registry_dep_rejects_unsafe_recipe_url);
	TEST_REGISTER(fetch_registry_dep_missing_recipe_url);
	TEST_REGISTER(toolcheck_skips_non_build_commands);
	TEST_REGISTER(toolcheck_fetch_needs_only_git);
	TEST_REGISTER(toolcheck_build_needs_clang);
	TEST_REGISTER(safe_strtol_valid);
	TEST_REGISTER(safe_strtol_invalid);
	TEST_REGISTER(safe_atol_valid);
	TEST_REGISTER(safe_atol_invalid);
	TEST_REGISTER(safe_fsync_dir_handles_any_path_shape);
	TEST_REGISTER(safe_header_exports_no_banned_wrappers);
}
