/*
 * Additional unit tests that raise per-function coverage (misc_commands).
 *
 * Every test runs a command handler inside a private mkdtemp() project
 * directory.  COFFEE_HOME points into that directory and PATH starts with a
 * directory of fake executables, so external tools (clang-tidy, doxygen, a C
 * compiler, ...) behave the same whether or not the real ones are installed.
 * Handler output (stdout and stderr) is captured and checked.
 */

#include "../src/build.h"
#include "../src/coffee.h"
#include "../src/registry.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_cov_misc_commands_tests(void);

/* ------------------------------------------------------------------ */
/* Fixture: private project directory, fake tools, sandboxed COFFEE_HOME */
/* ------------------------------------------------------------------ */

struct fixture {
	sds work;
	sds bin;
	sds saved_path;
	sds saved_home;
	sds saved_cc;
};

static char saved_cwd[4'096];

static sds env_save(const char *name)
{
	const char *value = getenv(name);
	if (value == nullptr) {
		return nullptr;
	}
	return sdsnew(value);
}

static void env_restore(const char *name, sds saved)
{
	if (saved != nullptr) {
		setenv(name, saved, 1);
		sdsfree(saved);
	} else {
		unsetenv(name);
	}
}

static bool write_file(const char *path, const char *content)
{
	FILE *fp = safe_fopen(path, "w");
	if (fp == nullptr) {
		return false;
	}
	fprintf_safe(fp, "%s", content);
	safe_fclose(fp);
	return true;
}

static sds read_file(const char *path)
{
	FILE *fp = safe_fopen(path, "r");
	if (fp == nullptr) {
		return nullptr;
	}
	sds    text = sdsempty();
	char   buf[1'024];
	size_t n = fread(buf, 1, sizeof(buf), fp);
	while (n > 0) {
		text = sdscatlen(text, buf, n);
		n    = fread(buf, 1, sizeof(buf), fp);
	}
	safe_fclose(fp);
	return text;
}

static bool has(const char *haystack, const char *needle)
{
	return haystack != nullptr && strstr(haystack, needle) != nullptr;
}

static bool file_has(const char *path, const char *needle)
{
	sds  text = read_file(path);
	bool ok   = has(text, needle);
	sdsfree(text);
	return ok;
}

static bool file_exists(const char *path)
{
	return safe_access(path, F_OK) == 0;
}

static bool fixture_enter(struct fixture *fx)
{
	fx->work = sdsnew("/tmp/cov-misc-XXXXXX");
	if (mkdtemp(fx->work) == nullptr || getcwd(saved_cwd, sizeof(saved_cwd)) == nullptr || chdir(fx->work) != 0) {
		sdsfree(fx->work);
		return false;
	}
	fx->bin  = sdscatprintf(sdsempty(), "%s/.fakebin", fx->work);
	sds home = sdscatprintf(sdsempty(), "%s/.home", fx->work);
	mkdir(fx->bin, 0755);
	mkdir(home, 0755);

	fx->saved_path = env_save("PATH");
	fx->saved_home = env_save("COFFEE_HOME");
	fx->saved_cc   = env_save("CC");
	sds path = sdscatprintf(sdsempty(), "%s:%s", fx->bin, fx->saved_path != nullptr ? fx->saved_path : "/usr/bin");
	setenv("PATH", path, 1);
	setenv("COFFEE_HOME", home, 1);
	sdsfree(path);
	sdsfree(home);
	return true;
}

static void fixture_leave(struct fixture *fx)
{
	env_restore("PATH", fx->saved_path);
	env_restore("COFFEE_HOME", fx->saved_home);
	env_restore("CC", fx->saved_cc);
	if (chdir(saved_cwd) != 0) {
		fprintf_safe(stderr, "warning: could not restore cwd\n");
	}
	test_remove_tree(fx->work);
	sdsfree(fx->work);
	sdsfree(fx->bin);
}

/* Install a fake executable; every call is appended to tool.log. */
static bool fake_tool(const struct fixture *fx, const char *name, const char *body)
{
	sds  path   = sdscatprintf(sdsempty(), "%s/%s", fx->bin, name);
	sds  script = sdscatprintf(sdsempty(), "#!/bin/sh\necho \"%s $*\" >> '%s/tool.log'\n%s\n", name, fx->work, body);
	bool ok     = write_file(path, script) && chmod(path, 0755) == 0;
	sdsfree(script);
	sdsfree(path);
	return ok;
}

static sds tool_log(const struct fixture *fx)
{
	sds path = sdscatprintf(sdsempty(), "%s/tool.log", fx->work);
	sds text = read_file(path);
	sdsfree(path);
	return text;
}

/* ------------------------------------------------------------------ */
/* Output capture                                                      */
/* ------------------------------------------------------------------ */

static int cap_out_fd = -1;
static int cap_err_fd = -1;
static sds cap_path   = nullptr;

static bool capture_start(void)
{
	fflush(stdout);
	fflush(stderr);
	cap_path = sdsnew("/tmp/cov-misc-cap-XXXXXX");
	int fd   = mkstemp(cap_path);
	if (fd < 0) {
		sdsfree(cap_path);
		cap_path = nullptr;
		return false;
	}
	cap_out_fd = dup(STDOUT_FILENO);
	cap_err_fd = dup(STDERR_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	safe_close(fd);
	return true;
}

static sds capture_stop(void)
{
	fflush(stdout);
	fflush(stderr);
	dup2(cap_out_fd, STDOUT_FILENO);
	dup2(cap_err_fd, STDERR_FILENO);
	safe_close(cap_out_fd);
	safe_close(cap_err_fd);
	sds text = read_file(cap_path);
	unlink(cap_path);
	sdsfree(cap_path);
	cap_path = nullptr;
	return text != nullptr ? text : sdsempty();
}

static i64 run_captured(i64 (*handler)(options *), options *opt, sds *out)
{
	if (!capture_start()) {
		*out = sdsempty();
		return -1;
	}
	i64 ret = handler(opt);
	*out    = capture_stop();
	return ret;
}

static i64 run_with(i64 (*handler)(options *), char **inputs, i64 count, sds *out)
{
	options opt = { .inputs = inputs, .inputs_num = count };
	return run_captured(handler, &opt, out);
}

/* Run a handler that takes only the command name. */
static i64 run_plain(i64 (*handler)(options *), char *name, sds *out)
{
	char *inputs[] = { name };
	return run_with(handler, inputs, 1, out);
}

/* ------------------------------------------------------------------ */
/* Project helpers                                                     */
/* ------------------------------------------------------------------ */

static const char *legacy_tail = "\n[compilation]\nMakefile = \"legacy\"\n";

/* Root-level `head` (e.g. a dependencies array) must precede [package]. */
static bool write_manifest(const char *head, const char *name, const char *tail)
{
	sds  text = sdscatprintf(sdsempty(),
	                         "%s[package]\nname = \"%s\"\nversion = \"1.2.3\"\nedition = \"c23\"\n"
	                         "description = \"A test package\"\nlicense = \"MIT\"\n%s",
	                         head != nullptr ? head : "", name, tail != nullptr ? tail : "");
	bool ok   = write_file("Coffee.toml", text);
	sdsfree(text);
	return ok;
}

static bool write_broken_manifest(void)
{
	return write_file("Coffee.toml", "[package\nname = \n");
}

static void make_dirs(const char *first, const char *second, const char *third)
{
	mkdir(first, 0755);
	if (second != nullptr) {
		mkdir(second, 0755);
	}
	if (third != nullptr) {
		mkdir(third, 0755);
	}
}

static bool write_sources(void)
{
	make_dirs("src", "tests", nullptr);
	return write_file("src/main.c", "int main(void)\n{\n\treturn 0;\n}\n") && write_file("tests/t1.c", "int t1;\n");
}

/* ------------------------------------------------------------------ */
/* bench                                                               */
/* ------------------------------------------------------------------ */

TEST(cov_misc_bench_legacy_runs_make_target)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "benchpkg", legacy_tail), "manifest");
	ASSERT(write_file("Makefile", "bench:\n\t@echo ran > bench.out\n"), "Makefile");

	sds  out = nullptr;
	i64  ret = run_plain(handle_bench, "bench", &out);
	bool ran = file_exists("bench.out");
	fixture_leave(&fx);

	ASSERT(ret == 0, "bench should succeed");
	ASSERT(ran, "make bench should have run");
	ASSERT(has(out, "Bench complete."), "success message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_bench_verbose_prints_command)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "benchpkg", legacy_tail), "manifest");
	ASSERT(write_file("Makefile", "bench:\n\t@true\n"), "Makefile");

	options opt = { .inputs = (char *[]){ "bench" }, .inputs_num = 1, .verbose = true };
	sds     out = nullptr;
	i64     ret = run_captured(handle_bench, &opt, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "bench should succeed");
	ASSERT(has(out, "Running: make bench -C "), "verbose prints the make command");
	ASSERT(has(out, "INC_FLAGS=-Isrc -Iinclude -I. -Ideps -I\"include/benchpkg\""), "include flags");
	ASSERT(has(out, "VERBOSE=1"), "verbose flag forwarded");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_bench_make_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "benchpkg", legacy_tail), "manifest");
	ASSERT(write_file("Makefile", "bench:\n\t@exit 1\n"), "Makefile");

	sds out = nullptr;
	i64 ret = run_plain(handle_bench, "bench", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "failing bench target should return 1");
	ASSERT(has(out, "Error: bench failed"), "failure message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_bench_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_bench, "bench", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse Coffee.toml"), "parse error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* cflags / libs                                                       */
/* ------------------------------------------------------------------ */

static const char *deps_head = "dependencies = [\"foo = 1.0\", \"bar\", \"ghost\"]\n";

static bool write_flag_deps(void)
{
	make_dirs("deps", "deps/foo", "deps/bar");
	make_dirs("deps/bar/include", "deps/bar/lib", nullptr);
	return write_file("deps/foo/library.toml",
	                  "include = [\"inc\", \"inc2\"]\nlib = [\"lib64\"]\nlibname = \"fooz\"\n");
}

TEST(cov_misc_cflags_from_manifest_dependencies)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(deps_head, "flagpkg", nullptr), "manifest");
	ASSERT(write_flag_deps(), "deps");

	sds out = nullptr;
	i64 ret = run_plain(handle_cflags, "cflags", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "cflags should succeed");
	ASSERT(strcmp(out, "-Ideps/foo/inc -Ideps/foo/inc2 -Ideps/bar/include\n") == 0, "include flags from library.toml");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_cflags_single_package)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_flag_deps(), "deps");

	char *inputs[] = { "cflags", "foo" };
	sds   out      = nullptr;
	i64   ret      = run_with(handle_cflags, inputs, 2, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "cflags foo should succeed");
	ASSERT(strcmp(out, "-Ideps/foo/inc -Ideps/foo/inc2\n") == 0, "flags for one package");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_cflags_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_cflags, "cflags", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse Coffee.toml"), "parse error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_libs_from_manifest_dependencies)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(deps_head, "flagpkg", nullptr), "manifest");
	ASSERT(write_flag_deps(), "deps");

	sds out = nullptr;
	i64 ret = run_plain(handle_libs, "libs", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "libs should succeed");
	ASSERT(strcmp(out, "-Ldeps/foo/lib64 -lfooz -Ldeps/bar/lib -lbar\n") == 0, "link flags honour libname and lib");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_libs_single_package)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_flag_deps(), "deps");

	char *inputs[] = { "libs", "foo" };
	sds   out      = nullptr;
	i64   ret      = run_with(handle_libs, inputs, 2, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "libs foo should succeed");
	ASSERT(strcmp(out, "-Ldeps/foo/lib64 -lfooz\n") == 0, "flags for one package");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_libs_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_libs, "libs", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse Coffee.toml"), "parse error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* check                                                               */
/* ------------------------------------------------------------------ */

static bool write_check_project(const struct fixture *fx, const char *cc_body)
{
	(void)fx;
	make_dirs("src", "include", "include/chk");
	make_dirs("deps", "deps/dep", "deps/dep/include");
	return write_file("Coffee.toml",
	                  "dependencies = [\"dep = 1.0\"]\n[package]\nname = \"chk\"\nversion = \"0.1.0\"\n") &&
	       write_file("src/main.c", "int main(void)\n{\n\treturn 0;\n}\n") &&
	       write_file("include/chk/chk.h", "#pragma once\n") && fake_tool(fx, "fakecc", cc_body);
}

TEST(cov_misc_check_reports_warnings_and_runs_compiler)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_check_project(&fx, "exit 0"), "project");
	sds cc = sdscatprintf(sdsempty(), "%s/fakecc", fx.bin);
	setenv("CC", cc, 1);
	sdsfree(cc);

	sds out = nullptr;
	i64 ret = run_plain(handle_check, "check", &out);
	sds log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "check should succeed");
	ASSERT(has(out, "Warning: [package] edition not set"), "missing edition is reported");
	ASSERT(has(out, "Manifest is valid."), "manifest verdict");
	ASSERT(has(out, "Check complete. No syntax errors found."), "final verdict");
	ASSERT(has(log, "-fsyntax-only"), "compiler invoked for syntax only");
	ASSERT(has(log, "-Ideps/dep/include -Ideps/dep"), "dependency include dirs passed");
	ASSERT(has(log, "-Iinclude/chk"), "project include dir passed");
	ASSERT(has(log, "src/main.c include/chk/chk.h"), "sources and headers passed");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_check_compiler_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_check_project(&fx, "exit 1"), "project");
	sds cc = sdscatprintf(sdsempty(), "%s/fakecc", fx.bin);
	setenv("CC", cc, 1);
	sdsfree(cc);

	sds out = nullptr;
	i64 ret = run_plain(handle_check, "check", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "compiler failure should return 1");
	ASSERT(has(out, "Check failed. Syntax errors found."), "failure message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* clean                                                               */
/* ------------------------------------------------------------------ */

TEST(cov_misc_clean_rm_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(fake_tool(&fx, "rm", "exit 1"), "fake rm");

	sds out = nullptr;
	i64 ret = run_plain(handle_clean, "clean", &out);
	sds log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 1, "failing rm should return 1");
	ASSERT(has(out, "Could not clean target directory"), "failure message");
	ASSERT(has(log, "rm -rf build"), "default target directory is build");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */

static sds config_file_path(void)
{
	return sdscatprintf(sdsempty(), "%s/config.toml", coffee_home_dir());
}

static bool write_config(const char *content)
{
	sds  path = config_file_path();
	bool ok   = write_file(path, content);
	sdsfree(path);
	return ok;
}

static sds read_config(void)
{
	sds path = config_file_path();
	sds text = read_file(path);
	sdsfree(path);
	return text;
}

/* Run `coffee config <a> [b] [c]` and return the exit status. */
static i64 run_config(char *a, char *b, char *c, sds *out)
{
	char *inputs[] = { "config", a, b, c };
	i64   count    = 1;
	for (size_t i = 1; i < 4 && inputs[i] != nullptr; i++) {
		count++;
	}
	return run_with(handle_config, inputs, count, out);
}

TEST(cov_misc_config_list_prints_top_level_keys)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("top = \"tv\"\nnum = 7\n"), "config");

	sds out = nullptr;
	i64 ret = run_config(nullptr, nullptr, nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "bare config lists");
	ASSERT(has(out, "top = \"tv\"\n"), "top-level key listed");
	ASSERT(!has(out, "num"), "integers are not listed");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_list_prints_section_keys)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("[sec]\nk = \"val\"\n[other]\nj = \"w\"\n"), "config");

	sds out = nullptr;
	i64 ret = run_config("--list", nullptr, nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "--list lists");
	ASSERT(has(out, "sec.k = \"val\"\n"), "first section key listed");
	ASSERT(has(out, "other.j = \"w\"\n"), "second section key listed");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_list_invalid_file)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("[broken\n= nope\n"), "config");

	sds out = nullptr;
	i64 ret = run_config("list", nullptr, nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "list of unparseable config succeeds");
	ASSERT(has(out, "(empty config)"), "empty config message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_get_integer_value)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("[sec]\nn = 42\n"), "config");

	sds out = nullptr;
	i64 ret = run_config("get", "sec.n", nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "get of integer succeeds");
	ASSERT(strcmp(out, "42\n") == 0, "integer printed in decimal");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_get_missing_key)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("[sec]\nk = \"v\"\n"), "config");

	sds out = nullptr;
	i64 ret = run_config("get", "sec.nope", nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "missing key returns 1");
	ASSERT(has(out, "key 'sec.nope' not found"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_get_without_config_file)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	sds out = nullptr;
	i64 ret = run_config("get", "plain", nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "no config file means key not found");
	ASSERT(has(out, "key 'plain' not found"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_set_inserts_before_next_section)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("[a]\nx = \"1\"\n[b]\ny = \"2\"\n"), "config");

	sds out = nullptr;
	i64 ret = run_config("set", "a.z", "3", &out);
	sds cfg = read_config();
	fixture_leave(&fx);

	ASSERT(ret == 0, "set succeeds");
	ASSERT(strcmp(cfg, "[a]\nx = \"1\"\nz = \"3\"\n[b]\ny = \"2\"\n") == 0, "key lands at the end of its section");
	sdsfree(out);
	sdsfree(cfg);
	PASS();
}

TEST(cov_misc_config_set_appends_to_last_section)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("[a]\nx = \"1\"\n"), "config");

	sds out = nullptr;
	i64 ret = run_config("set", "a.z", "3", &out);
	sds cfg = read_config();
	fixture_leave(&fx);

	ASSERT(ret == 0, "set succeeds");
	ASSERT(strcmp(cfg, "[a]\nx = \"1\"\nz = \"3\"\n") == 0, "key appended to the last section");
	sdsfree(out);
	sdsfree(cfg);
	PASS();
}

TEST(cov_misc_config_set_and_unset_unwritable_file)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds path = config_file_path();
	ASSERT(mkdir(path, 0755) == 0, "directory in place of the config file");
	sdsfree(path);

	sds set_out   = nullptr;
	sds unset_out = nullptr;
	i64 set_ret   = run_config("set", "k", "v", &set_out);
	i64 unset_ret = run_config("unset", "k", nullptr, &unset_out);
	fixture_leave(&fx);

	ASSERT(set_ret == 1, "set fails when the file cannot be written");
	ASSERT(has(set_out, "Could not write"), "set error message");
	ASSERT(unset_ret == 1, "unset fails when the file cannot be written");
	sdsfree(set_out);
	sdsfree(unset_out);
	PASS();
}

TEST(cov_misc_config_usage_errors)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	sds set_out   = nullptr;
	sds unset_out = nullptr;
	sds bad_out   = nullptr;
	i64 set_ret   = run_config("set", "only-key", nullptr, &set_out);
	i64 unset_ret = run_config("unset", nullptr, nullptr, &unset_out);
	i64 bad_ret   = run_config("frobnicate", nullptr, nullptr, &bad_out);
	fixture_leave(&fx);

	ASSERT(set_ret == 1 && has(set_out, "Usage: coffee config set"), "set needs key and value");
	ASSERT(unset_ret == 1 && has(unset_out, "Usage: coffee config unset"), "unset needs a key");
	ASSERT(bad_ret == 1 && has(bad_out, "Unknown config subcommand 'frobnicate'"), "unknown subcommand");
	sdsfree(set_out);
	sdsfree(unset_out);
	sdsfree(bad_out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* doc                                                                 */
/* ------------------------------------------------------------------ */

static i64 run_doc(char *sub, sds *out)
{
	char *inputs[] = { "doc", sub };
	return run_with(handle_doc, inputs, sub != nullptr ? 2 : 1, out);
}

TEST(cov_misc_doc_check_found_and_missing)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(fake_tool(&fx, "doxygen", "exit 0"), "fake doxygen");
	sds found_out = nullptr;
	i64 found_ret = run_doc("check", &found_out);
	ASSERT(fake_tool(&fx, "doxygen", "exit 1"), "failing fake doxygen");
	sds missing_out = nullptr;
	i64 missing_ret = run_doc("check", &missing_out);
	fixture_leave(&fx);

	ASSERT(found_ret == 0 && has(found_out, "doxygen found."), "doxygen present");
	ASSERT(missing_ret == 1 && has(missing_out, "doxygen not found."), "doxygen absent");
	sdsfree(found_out);
	sdsfree(missing_out);
	PASS();
}

TEST(cov_misc_doc_unknown_subcommand)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	sds out = nullptr;
	i64 ret = run_doc("bogus", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "unknown subcommand returns 1");
	ASSERT(has(out, "Unknown subcommand: bogus"), "error message");
	ASSERT(has(out, "Usage: coffee doc [generate|check]"), "usage hint");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_doc_manifest_errors)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	sds missing_out = nullptr;
	i64 missing_ret = run_doc("generate", &missing_out);
	ASSERT(write_broken_manifest(), "manifest");
	sds broken_out = nullptr;
	i64 broken_ret = run_doc("generate", &broken_out);
	fixture_leave(&fx);

	/* The sandbox lives under /tmp, so no Coffee.toml is found above it. */
	ASSERT(missing_ret == 1 && has(missing_out, "Could not find Coffee.toml"), "no manifest");
	ASSERT(broken_ret == 1 && has(broken_out, "Could not parse Coffee.toml"), "broken manifest");
	sdsfree(missing_out);
	sdsfree(broken_out);
	PASS();
}

TEST(cov_misc_doc_generate_doxyfile_from_doc_section)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "docpkg",
	                      "\n[doc]\nproject-name = \"Shown\"\noutput-dir = \"elsewhere\"\ninput-dirs = \"src lib\"\n"
	                      "exclude-patterns = \"*/tests/*\"\n"),
	       "manifest");
	ASSERT(fake_tool(&fx, "doxygen", "exit 0"), "fake doxygen");

	sds out      = nullptr;
	i64 ret      = run_doc("generate", &out);
	sds doxyfile = read_file("Doxyfile");
	sds log      = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "generate should succeed");
	ASSERT(has(out, "Generated Doxyfile"), "Doxyfile generated");
	ASSERT(has(out, "output-dir is ignored"), "output-dir warning");
	ASSERT(has(out, "Documentation complete."), "final message");
	ASSERT(has(doxyfile, "PROJECT_NAME           = \"Shown\""), "project name from [doc]");
	ASSERT(has(doxyfile, "PROJECT_NUMBER         = 1.2.3"), "version from [package]");
	ASSERT(has(doxyfile, "INPUT                  = src lib"), "input dirs from [doc]");
	ASSERT(has(doxyfile, "EXCLUDE_PATTERNS       = */tests/*"), "exclude patterns from [doc]");
	ASSERT(has(log, "doxygen"), "doxygen was run");
	sdsfree(out);
	sdsfree(doxyfile);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_doc_generate_defaults_for_empty_doc_section)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "docpkg", "\n[doc]\n"), "manifest");
	ASSERT(fake_tool(&fx, "doxygen", "exit 1"), "failing fake doxygen");

	sds out      = nullptr;
	i64 ret      = run_doc(nullptr, &out);
	sds doxyfile = read_file("Doxyfile");
	fixture_leave(&fx);

	ASSERT(ret == 1, "doxygen failure is reported");
	ASSERT(has(out, "Documentation generation failed"), "failure message");
	ASSERT(has(doxyfile, "PROJECT_NAME           = \"docpkg\""), "default project name");
	ASSERT(has(doxyfile, "INPUT                  = src include"), "default input dirs");
	ASSERT(!has(doxyfile, "EXCLUDE_PATTERNS"), "no exclude patterns by default");
	sdsfree(out);
	sdsfree(doxyfile);
	PASS();
}

TEST(cov_misc_doc_generate_unwritable_doxyfile)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "docpkg", "\n[doc]\n"), "manifest");
	ASSERT(symlink("no-such-dir/Doxyfile", "Doxyfile") == 0, "dangling Doxyfile symlink");

	sds out = nullptr;
	i64 ret = run_doc("generate", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "unwritable Doxyfile returns 1");
	ASSERT(has(out, "Could not create Doxyfile"), "error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* fix / lint / fmt / grep                                             */
/* ------------------------------------------------------------------ */

TEST(cov_misc_fix_verbose_success)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "fixpkg", nullptr), "manifest");
	ASSERT(write_sources(), "sources");
	ASSERT(fake_tool(&fx, "find", "exit 0"), "fake find");

	options opt = { .inputs = (char *[]){ "fix" }, .inputs_num = 1, .verbose = true };
	sds     out = nullptr;
	i64     ret = run_captured(handle_fix, &opt, &out);
	sds     log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "fix should succeed");
	ASSERT(has(out, "Running: find src tests -name *.c -exec clang-tidy --fix --quiet --extra-arg=-Isrc"),
	       "verbose command line");
	ASSERT(has(out, "Fixes applied successfully"), "success message");
	ASSERT(has(log, "find src tests -name *.c -exec clang-tidy --fix --quiet --extra-arg=-Isrc --extra-arg=-Iinclude "
	                "--extra-arg=-I. --extra-arg=-Ideps --extra-arg=-Iinclude/fixpkg {} +"),
	       "find command with manifest-derived include flags");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_lint_fix_verbose_success)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "lintpkg", nullptr), "manifest");
	ASSERT(write_sources(), "sources");
	ASSERT(fake_tool(&fx, "find", "exit 0"), "fake find");

	options opt = { .inputs = (char *[]){ "lint" }, .inputs_num = 1, .verbose = true, .fix = true };
	sds     out = nullptr;
	i64     ret = run_captured(handle_lint, &opt, &out);
	sds     log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "lint should succeed");
	ASSERT(has(out, "Running: find src tests"), "verbose command line");
	ASSERT(has(out, "Linting complete."), "success message");
	ASSERT(has(log, "-exec clang-tidy --fix --quiet --extra-arg=-Isrc"), "--fix forwarded to clang-tidy");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_fmt_formatter_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_sources(), "sources");
	ASSERT(fake_tool(&fx, "clang-format", "exit 1"), "fake clang-format");

	sds out = nullptr;
	i64 ret = run_plain(handle_fmt, "fmt", &out);
	sds log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 1, "formatter failure returns 1");
	ASSERT(has(out, "Formatting failed. Please ensure 'clang-format' is installed."), "failure message");
	ASSERT(has(log, "clang-format -i"), "clang-format ran in place");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_grep_pattern_not_found)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_sources(), "sources");

	char *inputs[] = { "grep", "zzz_no_such_token_zzz" };
	sds   out      = nullptr;
	i64   ret      = run_with(handle_grep, inputs, 2, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "grep always returns 0 when a pattern is given");
	ASSERT(has(out, "Searching for 'zzz_no_such_token_zzz'..."), "search banner");
	ASSERT(has(out, "Pattern not found."), "not-found message");
	sdsfree(out);
	PASS();
}

/*
 * A symlinked Coffee.toml whose real location is deeper than PATH_MAX: the
 * manifest is found, but realpath() cannot return its canonical path.
 */
#define DEEP_NAME_LEN 200
#define DEEP_LEVELS   25
#define DEEP_JUMP     15

static sds deep_chain(const char *name, size_t levels)
{
	sds chain = sdsempty();
	for (size_t i = 0; i < levels; i++) {
		chain = sdscatfmt(chain, "%s%s", i > 0 ? "/" : "", name);
	}
	return chain;
}

static size_t deep_create(const char *name)
{
	size_t levels = 0;
	while (levels < DEEP_LEVELS && mkdir(name, 0755) == 0 && chdir(name) == 0) {
		levels++;
	}
	return levels;
}

static void deep_remove(const char *work, const char *name, size_t levels)
{
	if (chdir(work) != 0) {
		return;
	}
	for (size_t i = 0; i < levels; i++) {
		if (chdir(name) != 0) {
			break;
		}
	}
	unlink("Coffee.toml");
	for (size_t i = 0; i < levels; i++) {
		if (chdir("..") == 0) {
			rmdir(name);
		}
	}
}

TEST(cov_misc_locate_project_path_longer_than_path_max)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds name = sdsnewlen(nullptr, DEEP_NAME_LEN);
	memset(name, 'd', DEEP_NAME_LEN);

	size_t levels = deep_create(name);
	bool   built  = levels == DEEP_LEVELS && write_manifest(nullptr, "deeppkg", nullptr);
	ASSERT(chdir(fx.work) == 0, "back to the project dir");
	sds jump_target = deep_chain(name, DEEP_JUMP);
	sds rest        = deep_chain(name, DEEP_LEVELS - DEEP_JUMP);
	sds toml_target = sdscatfmt(sdsempty(), "jump/%S/Coffee.toml", rest);
	built           = built && symlink(jump_target, "jump") == 0 && symlink(toml_target, "Coffee.toml") == 0;

	sds out = nullptr;
	i64 ret = built ? run_plain(handle_locate_project, "locate-project", &out) : -1;
	unlink("jump");
	deep_remove(fx.work, name, levels);
	fixture_leave(&fx);

	ASSERT(built, "deep layout");
	ASSERT(ret == 1, "unresolvable manifest path returns 1");
	ASSERT(has(out, "Could not resolve absolute path for manifest"), "error message");
	sdsfree(out);
	sdsfree(name);
	sdsfree(jump_target);
	sdsfree(rest);
	sdsfree(toml_target);
	PASS();
}

/* ------------------------------------------------------------------ */
/* info / list / pkgid / locate-project                                */
/* ------------------------------------------------------------------ */

TEST(cov_misc_info_prints_license_and_dependencies)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest("dependencies = [\"alpha\", \"beta = 2.0\"]\n", "infopkg", nullptr), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_info, "info", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "info should succeed");
	ASSERT(has(out, "  Name:        infopkg\n"), "name");
	ASSERT(has(out, "  License:     MIT\n"), "license");
	ASSERT(has(out, "  Dependencies (2):\n    - alpha\n    - beta = \"2.0\"\n") ||
	           has(out, "  Dependencies (2):\n    - alpha\n    - beta = 2.0\n"),
	       "dependency list");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_info_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_info, "info", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse manifest"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_pkgid_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_pkgid, "pkgid", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse manifest"), "error message");
	sdsfree(out);
	PASS();
}

static i64 run_list_installed(sds *out)
{
	char *inputs[] = { "list", "installed" };
	return run_with(handle_list, inputs, 2, out);
}

TEST(cov_misc_list_installed_without_bin_dir)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	sds out = nullptr;
	i64 ret = run_list_installed(&out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "list installed succeeds");
	ASSERT(has(out, "No installed binaries."), "no bin dir message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_list_installed_bin_is_not_a_directory)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds bin_path = sdscatprintf(sdsempty(), "%s/bin", coffee_home_dir());
	ASSERT(write_file(bin_path, "not a directory\n"), "file in place of bin");
	sdsfree(bin_path);

	sds out = nullptr;
	i64 ret = run_list_installed(&out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "list installed succeeds");
	ASSERT(has(out, "No installed binaries."), "unreadable bin dir message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_list_installed_empty_and_populated)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds bin_dir = sdscatprintf(sdsempty(), "%s/bin", coffee_home_dir());
	ASSERT(mkdir(bin_dir, 0755) == 0, "bin dir");

	sds empty_out = nullptr;
	i64 empty_ret = run_list_installed(&empty_out);

	sds tool = sdscatprintf(sdsempty(), "%s/mytool", bin_dir);
	sds data = sdscatprintf(sdsempty(), "%s/notes.txt", bin_dir);
	ASSERT(write_file(tool, "#!/bin/sh\n") && chmod(tool, 0755) == 0, "executable");
	ASSERT(write_file(data, "text\n"), "non-executable file");
	sds full_out = nullptr;
	i64 full_ret = run_list_installed(&full_out);
	sdsfree(tool);
	sdsfree(data);
	sdsfree(bin_dir);
	fixture_leave(&fx);

	ASSERT(empty_ret == 0 && has(empty_out, "  (none)"), "empty bin dir");
	ASSERT(full_ret == 0 && has(full_out, "  mytool\n"), "executable listed");
	ASSERT(!has(full_out, "notes.txt"), "non-executable skipped");
	sdsfree(empty_out);
	sdsfree(full_out);
	PASS();
}

TEST(cov_misc_list_project_dependencies)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest("dependencies = [\"alpha\", \"beta\"]\n", "listpkg", nullptr), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_list, "list", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "list should succeed");
	ASSERT(has(out, "Package: listpkg\nDependencies:\n  alpha\n  beta\n"), "dependencies listed");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_list_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_list, "list", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Failed to parse manifest"), "error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* machete                                                             */
/* ------------------------------------------------------------------ */

static bool write_machete_sources(void)
{
	make_dirs("src", "src/sub", ".hidden");
	return write_file("src/main.c", "#include <stdio.h>\n  #include <usedlib/api.h>\nint main(void) { return 0; }\n") &&
	       write_file("src/sub/other.cpp", "// not an include\n") && write_file("src/sub/README", "text\n") &&
	       write_file("src/x", "text\n") && write_file(".hidden/skip.c", "#include <hiddenlib.h>\n") &&
	       symlink("no-such-target", "src/dangling.c") == 0 && write_file("src/locked.c", "#include <x.h>\n") &&
	       chmod("src/locked.c", 0) == 0 && mkdir("src/locked", 0755) == 0 && chmod("src/locked", 0) == 0;
}

/* Let the fixture remove the unreadable entries again. */
static void restore_machete_permissions(void)
{
	chmod("src/locked.c", 0644);
	chmod("src/locked", 0755);
}

TEST(cov_misc_machete_reports_unused_dependencies)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest("dependencies = [\"usedlib = 1.0\", \"unusedlib\", \"hiddenlib\"]\n", "macpkg", nullptr),
	       "manifest");
	ASSERT(write_machete_sources(), "sources");

	sds out = nullptr;
	i64 ret = run_plain(handle_machete, "machete", &out);
	restore_machete_permissions();
	fixture_leave(&fx);

	ASSERT(ret == 0, "machete should succeed");
	ASSERT(has(out, "Unused dependencies:\n  unusedlib\n  hiddenlib\n"), "unused dependencies listed");
	ASSERT(!has(out, "usedlib\n  unusedlib") && !has(out, "  usedlib\n"), "used dependency not listed");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_machete_all_dependencies_used)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest("dependencies = [\"usedlib\"]\n", "macpkg", nullptr), "manifest");
	ASSERT(write_machete_sources(), "sources");

	sds out = nullptr;
	i64 ret = run_plain(handle_machete, "machete", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "machete should succeed");
	ASSERT(has(out, "No unused dependencies found."), "all used");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_machete_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_machete, "machete", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse Coffee.toml"), "error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* metadata                                                            */
/* ------------------------------------------------------------------ */

TEST(cov_misc_metadata_features_and_transitive_dependencies)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "metapkg",
	                      "\n[features]\ndefault = [\"json\"]\njson = []\n\n[dependencies]\n"
	                      "liba = { git = \"https://example.invalid/liba.git\", branch = \"dev\" }\nlibb = \"1.0\"\n"),
	       "manifest");
	make_dirs("deps", "deps/liba", "deps/libb");
	make_dirs("deps/liba/.git", nullptr, nullptr);

	sds out = nullptr;
	i64 ret = run_plain(handle_metadata, "metadata", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "metadata should succeed");
	ASSERT(has(out, "\"features\": {\n    \"metapkg\": [\"json\"]\n  },"), "resolved default feature");
	ASSERT(has(out, "\"transitive_deps\""), "transitive section");
	ASSERT(
	    has(out, "\"name\": \"liba\",\n      \"path\": \"deps/liba\",\n      \"git\": true,\n      \"ref\": \"dev\""),
	    "git dependency with ref");
	ASSERT(has(out, "    },\n    {\n      \"name\": \""), "dependencies are comma separated");
	ASSERT(has(out, "\"name\": \"libb\",\n      \"path\": \"deps/libb\",\n      \"git\": false"), "path dependency");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_metadata_unresolvable_graph)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "bad name", nullptr), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_metadata, "metadata", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "metadata still prints the manifest part");
	ASSERT(!has(out, "transitive_deps"), "no transitive section without a graph");
	ASSERT(has(out, "\"dependencies\": [\n  ]\n}\n"), "closing JSON");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_metadata_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_metadata, "metadata", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse manifest"), "error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* new / init                                                          */
/* ------------------------------------------------------------------ */

TEST(cov_misc_new_cannot_create_directory)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	char *inputs[] = { "new", "no-parent/child" };
	sds   out      = nullptr;
	i64   ret      = run_with(handle_new, inputs, 2, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "missing parent directory returns 1");
	ASSERT(has(out, "Could not create project directory"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_new_refuses_existing_project)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	char *inputs[]   = { "new", "proj" };
	sds   first      = nullptr;
	i64   first_ret  = run_with(handle_new, inputs, 2, &first);
	sds   second     = nullptr;
	i64   second_ret = run_with(handle_new, inputs, 2, &second);
	fixture_leave(&fx);

	ASSERT(first_ret == 0, "first new succeeds");
	ASSERT(second_ret == 1, "second new fails");
	ASSERT(has(second, "Project already exists at proj"), "error message");
	sdsfree(first);
	sdsfree(second);
	PASS();
}

TEST(cov_misc_new_trailing_slash_uses_default_name)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	char *inputs[] = { "new", "some-dir/" };
	sds   out      = nullptr;
	i64   ret      = run_with(handle_new, inputs, 2, &out);
	bool  header   = file_exists("some-dir/include/project/project.h");
	fixture_leave(&fx);

	ASSERT(ret == 0, "new with trailing slash succeeds");
	ASSERT(has(out, "Created new C project: project"), "default project name");
	ASSERT(header, "header named after the default project");
	sdsfree(out);
	PASS();
}

/* One entry per step of handle_init that can fail: the path that blocks it. */
struct init_blocker {
	const char *parent_a;
	const char *parent_b;
	const char *path;
	const char *target;
	const char *message;
};

/* A non-null target makes the blocker a dangling symlink, otherwise a
 * directory (which cannot be opened for writing as a file). */
static const struct init_blocker init_blockers[] = {
	{ nullptr, nullptr, "include", "no-such-target", "Could not create include directory" },
	{ nullptr, nullptr, "src", "no-such-target", "Could not create src directory" },
	{ nullptr, nullptr, "deps", "no-such-target", "Could not create deps directory" },
	{ nullptr, nullptr, "tests", "no-such-target", "Could not create tests directory" },
	{ nullptr, nullptr, "doc", "no-such-target", "Could not create doc directory" },
	{ nullptr, nullptr, "scripts", "no-such-target", "Could not create scripts directory" },
	{ nullptr, nullptr, "build", "no-such-target", "Could not create build directory" },
	{ "include", nullptr, "include/blk", "no-such-target", "Could not create include/blk directory" },
	{ nullptr, nullptr, ".gitignore", nullptr, "Could not create .gitignore" },
	{ nullptr, nullptr, "LICENSE", nullptr, "Could not create LICENSE" },
	{ nullptr, nullptr, "README.md", nullptr, "Could not create README.md" },
	{ "src", nullptr, "src/main.c", nullptr, "Could not create main.c" },
	{ nullptr, nullptr, "Makefile", nullptr, "Could not create Makefile" },
	{ "include", "include/blk", "include/blk/blk.h", nullptr, "Could not create include/blk/blk.h" },
	{ nullptr, nullptr, "Coffee.toml", "no-such-dir/x", "Could not create Coffee.toml" },
};

static bool place_init_blocker(const struct init_blocker *blocker)
{
	if (blocker->parent_a != nullptr) {
		mkdir(blocker->parent_a, 0755);
	}
	if (blocker->parent_b != nullptr) {
		mkdir(blocker->parent_b, 0755);
	}
	if (blocker->target != nullptr) {
		return symlink(blocker->target, blocker->path) == 0;
	}
	return mkdir(blocker->path, 0755) == 0;
}

TEST(cov_misc_init_reports_each_creation_failure)
{
	for (size_t i = 0; i < sizeof(init_blockers) / sizeof(init_blockers[0]); i++) {
		struct fixture fx;
		ASSERT(fixture_enter(&fx), "fixture");
		bool placed = place_init_blocker(&init_blockers[i]);

		char *inputs[] = { "init", "blk" };
		sds   out      = nullptr;
		i64   ret      = run_with(handle_init, inputs, 2, &out);
		bool  matches  = ret == 1 && has(out, init_blockers[i].message);
		if (!matches) {
			printf("  blocker %s: ret=%lld output=%s\n", init_blockers[i].path, (long long)ret, out);
		}
		sdsfree(out);
		fixture_leave(&fx);

		ASSERT(placed, "blocker placed");
		ASSERT(matches, "init reports the blocked step");
	}
	PASS();
}

TEST(cov_misc_init_in_removed_directory_falls_back_to_default_name)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(mkdir("gone", 0755) == 0, "gone dir");
	ASSERT(chdir("gone") == 0, "enter gone dir");
	ASSERT(rmdir("../gone") == 0, "remove current directory");

	sds out = nullptr;
	i64 ret = run_plain(handle_init, "init", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "init cannot create directories in a removed cwd");
	ASSERT(has(out, "Could not create include directory"), "error message");
	sdsfree(out);
	PASS();
}

/*
 * src/main.c already exists (older directory entry) and unreadable .c files are
 * created after it, so readdir() may visit an unreadable file first.
 */
TEST(cov_misc_init_skips_unreadable_c_files)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(mkdir("src", 0755) == 0, "src dir");
	ASSERT(write_file("src/main.c", "int main(void)\n{\n\treturn 0;\n}\n"), "main.c");
	bool made = true;
	for (int i = 0; i < 20; i++) {
		sds path = sdscatprintf(sdsempty(), "src/locked%d.c", i);
		made     = made && write_file(path, "int x;\n") && chmod(path, 0) == 0;
		sdsfree(path);
	}

	sds  out = nullptr;
	i64  ret = run_plain(handle_init, "init", &out);
	bool bin = file_has("Coffee.toml", "[[bin]]");
	fixture_leave(&fx);

	ASSERT(made, "unreadable files");
	ASSERT(ret == 0, "init succeeds");
	ASSERT(bin, "main.c is still detected");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* package                                                             */
/* ------------------------------------------------------------------ */

TEST(cov_misc_package_creates_tarball)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "pkgpkg", nullptr), "manifest");
	ASSERT(fake_tool(&fx, "tar", "touch \"$2\""), "fake tar");

	sds  out  = nullptr;
	i64  ret  = run_plain(handle_package, "package", &out);
	sds  log  = tool_log(&fx);
	bool made = file_exists("build/package/pkgpkg-1.2.3.tar.gz");
	fixture_leave(&fx);

	ASSERT(ret == 0, "package should succeed");
	ASSERT(has(out, "Package created: build/package/pkgpkg-1.2.3.tar.gz"), "success message");
	ASSERT(has(log, "tar -czf build/package/pkgpkg-1.2.3.tar.gz Coffee.toml src/ tests/"), "tar arguments");
	ASSERT(made, "tarball written");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_package_tar_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "pkgpkg", nullptr), "manifest");
	ASSERT(fake_tool(&fx, "tar", "exit 2"), "fake tar");

	sds out = nullptr;
	i64 ret = run_plain(handle_package, "package", &out);
	fixture_leave(&fx);

	ASSERT(ret == 2, "tar exit status is propagated");
	ASSERT(has(out, "Error: Packaging failed."), "failure message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_package_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_package, "package", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse manifest"), "error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* report                                                              */
/* ------------------------------------------------------------------ */

static const char *report_deps_tail = "\n[dependencies]\n"
                                      "gitdep = { git = \"https://example.invalid/gitdep.git\" }\n"
                                      "pathdep = { path = \"deps/pathdep\" }\n"
                                      "plain = \"1.0\"\n";

static i64 run_report(char *type, sds *out)
{
	char *inputs[] = { "report", type };
	return run_with(handle_report, inputs, type != nullptr ? 2 : 1, out);
}

TEST(cov_misc_report_deps_classifies_sources)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "reppkg", report_deps_tail), "manifest");

	sds out = nullptr;
	i64 ret = run_report(nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "report should succeed");
	ASSERT(has(out, "gitdep               git\n"), "git dependency");
	ASSERT(has(out, "pathdep              path\n"), "path dependency");
	ASSERT(has(out, "plain                unknown\n"), "plain version dependency");
	ASSERT(has(out, "Total: 3 dependencies"), "total");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_report_unknown_type)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "reppkg", nullptr), "manifest");

	sds out = nullptr;
	i64 ret = run_report("nonsense", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "unknown type returns 1");
	ASSERT(has(out, "Unknown report type 'nonsense'"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_report_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_report("deps", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse Coffee.toml"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_report_audit_unresolvable_graph)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest("dependencies = [\"alpha\"]\n", "bad name", nullptr), "manifest");

	sds out = nullptr;
	i64 ret = run_report("audit", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "audit still returns 0");
	ASSERT(has(out, "Could not resolve dependency graph."), "message");
	sdsfree(out);
	PASS();
}

/* Fake git: `rev-list` prints $1 of git-behind.txt, everything else succeeds. */
static bool install_fake_git(const struct fixture *fx, const char *fetch_status)
{
	sds  body = sdscatprintf(sdsempty(), "case \"$*\" in\n*fetch*) exit %s;;\n*rev-list*) cat git-behind.txt;;\nesac\n",
	                         fetch_status);
	bool ok   = fake_tool(fx, "git", body);
	sdsfree(body);
	return ok;
}

static bool write_audit_project(const char *behind)
{
	if (!write_manifest(nullptr, "reppkg",
	                    "\n[dependencies]\ngitdep = { git = \"https://example.invalid/gitdep.git\", branch = "
	                    "\"main\" }\npathdep = { path = \"deps/pathdep\" }\nlostdep = { path = \"deps/lostdep\" }\n"
	                    "plain = \"1.0\"\n")) {
		return false;
	}
	make_dirs("deps", "deps/gitdep", "deps/pathdep");
	make_dirs("deps/gitdep/.git", nullptr, nullptr);
	return write_file("git-behind.txt", behind);
}

TEST(cov_misc_report_audit_up_to_date_and_path)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_audit_project("0\n"), "project");
	ASSERT(install_fake_git(&fx, "0"), "fake git");

	sds out = nullptr;
	i64 ret = run_report("audit", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "audit should succeed");
	ASSERT(has(out, "gitdep               OK (main up-to-date)"), "up-to-date git dependency");
	ASSERT(has(out, "pathdep              OK (path: deps/pathdep)"), "path dependency");
	ASSERT(has(out, "lostdep              NOT FOUND"), "missing path dependency");
	ASSERT(has(out, "plain                NOT FOUND"), "version-only dependency without a directory");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_report_audit_outdated)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_audit_project("3\n"), "project");
	ASSERT(install_fake_git(&fx, "0"), "fake git");

	sds out = nullptr;
	i64 ret = run_report("audit", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "audit should succeed");
	ASSERT(has(out, "gitdep               OUTDATED (3 commits behind main)"), "outdated git dependency");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_report_audit_fetch_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_audit_project("0\n"), "project");
	ASSERT(install_fake_git(&fx, "1"), "fake git");

	sds out = nullptr;
	i64 ret = run_report("audit", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "audit should succeed");
	ASSERT(has(out, "gitdep               (fetch failed)"), "fetch failure reported");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* run                                                                 */
/* ------------------------------------------------------------------ */

static const char *run_makefile_no_exe = "all:\n\t@true\n";

static const char *run_makefile_with_exe = "all:\n"
                                           "\t@mkdir -p build\n"
                                           "\t@printf '#!/bin/sh\\necho \"$$@\" > args.txt\\n' > build/runpkg\n"
                                           "\t@chmod +x build/runpkg\n";

TEST(cov_misc_run_executable_missing)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "runpkg", legacy_tail), "manifest");
	ASSERT(write_file("Makefile", run_makefile_no_exe), "Makefile");

	sds out = nullptr;
	i64 ret = run_plain(handle_run, "run", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "missing executable returns 1");
	ASSERT(has(out, "Error: Executable not found: ") && has(out, "/build/runpkg"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_run_forwards_arguments)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "runpkg", legacy_tail), "manifest");
	ASSERT(write_file("Makefile", run_makefile_with_exe), "Makefile");

	char *inputs[] = { "run", "first", "second" };
	sds   out      = nullptr;
	i64   ret      = run_with(handle_run, inputs, 3, &out);
	bool  args_ok  = file_has("args.txt", "first second");
	fixture_leave(&fx);

	ASSERT(ret == 0, "run should succeed");
	ASSERT(args_ok, "program received the arguments");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* search                                                              */
/* ------------------------------------------------------------------ */

static bool write_package_index(const char *json)
{
	sds  path = sdscatprintf(sdsempty(), "%s/packages.json", coffee_home_dir());
	bool ok   = write_file(path, json);
	sdsfree(path);
	return ok;
}

static i64 run_search(char *query, sds *out)
{
	char *inputs[] = { "search", query };
	return run_with(handle_search, inputs, query != nullptr ? 2 : 1, out);
}

TEST(cov_misc_search_lists_matching_packages)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_package_index("[\n {\n  \"name\": \"alpha\",\n  \"version\": \"1.0\",\n"
	                           "  \"description\": \"First lib\"\n },\n {\n  \"name\": \"beta\",\n"
	                           "  \"version\": \"2.0\",\n  \"description\": \"Second lib\"\n }\n]\n"),
	       "index");

	sds all_out  = nullptr;
	i64 all_ret  = run_search(nullptr, &all_out);
	sds one_out  = nullptr;
	i64 one_ret  = run_search("ALP", &one_out);
	sds none_out = nullptr;
	i64 none_ret = run_search("zzz", &none_out);
	fixture_leave(&fx);

	ASSERT(all_ret == 0 && has(all_out, "alpha") && has(all_out, "beta"), "all packages listed");
	ASSERT(one_ret == 0 && has(one_out, "alpha") && !has(one_out, "beta"), "case-insensitive filter");
	ASSERT(has(one_out, "1.0          First lib"), "columns");
	ASSERT(none_ret == 0 && has(none_out, "No packages found."), "no match");
	sdsfree(all_out);
	sdsfree(one_out);
	sdsfree(none_out);
	PASS();
}

TEST(cov_misc_search_empty_index)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_package_index("[]\n"), "index");

	sds out = nullptr;
	i64 ret = run_search(nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "search succeeds");
	ASSERT(has(out, "No packages found."), "empty index message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* test                                                                */
/* ------------------------------------------------------------------ */

/* Fake compiler: records its arguments in cc.args and "builds" the -o
 * output by copying runner.tpl, a script that records its own arguments. */
static const char *fake_cc_body = "out=''\n"
                                  "prev=''\n"
                                  "for a in \"$@\"; do\n"
                                  "\tif [ \"$prev\" = '-o' ]; then out=\"$a\"; fi\n"
                                  "\tprev=\"$a\"\n"
                                  "\tprintf '%s\\n' \"$a\" >> cc.args\n"
                                  "done\n"
                                  "cp runner.tpl \"$out\" && chmod +x \"$out\"\n";

static const char *runner_script = "#!/bin/sh\nprintf '%s\\n' \"$@\" > runner.args\nexit 0\n";

static bool write_test_project(struct fixture *fx, const char *head, const char *name, const char *tail)
{
	make_dirs("src", "tests", nullptr);
	if (!write_manifest(head, name, tail) || !write_file("src/main.c", "int main(void) { return 0; }\n") ||
	    !write_file("src/util.c", "int util;\n") || !write_file("tests/t1.c", "int t1;\n") ||
	    !write_file("tests/t2.c", "int t2;\n") || !write_file("runner.tpl", runner_script) ||
	    !fake_tool(fx, "fakecc", fake_cc_body)) {
		return false;
	}
	sds cc = sdscatprintf(sdsempty(), "%s/fakecc", fx->bin);
	setenv("CC", cc, 1);
	sdsfree(cc);
	return true;
}

static i64 run_test_cmd(const char *filter, bool verbose, sds *out)
{
	char   *inputs[] = { "test", unconst(filter) };
	options opt      = { .inputs = inputs, .inputs_num = filter != nullptr ? 2 : 1, .verbose = verbose };
	return run_captured(handle_test, &opt, out);
}

TEST(cov_misc_test_builds_and_runs_with_filter)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds saved_filter = env_save("TEST_FILTER");
	unsetenv("TEST_FILTER");
	ASSERT(write_test_project(&fx, nullptr, "tproj", nullptr), "project");

	sds  out     = nullptr;
	i64  ret     = run_test_cmd("only_this", false, &out);
	sds  cc_args = read_file("cc.args");
	bool ran     = file_has("runner.args", "only_this\n");
	env_restore("TEST_FILTER", saved_filter);
	fixture_leave(&fx);

	ASSERT(ret == 0, "test run should succeed");
	ASSERT(ran, "runner received the filter");
	ASSERT(has(cc_args, "-DCOFFEE_TEST_RUNNER\n"), "test runner macro defined");
	ASSERT(has(cc_args, "-o\nbuild/debug/tproj-tests\n"), "default output path");
	ASSERT(has(cc_args, "/src/main.c\n") && has(cc_args, "/tests/t1.c\n") && has(cc_args, "/tests/t2.c\n"),
	       "project and test sources compiled");
	sdsfree(out);
	sdsfree(cc_args);
	PASS();
}

TEST(cov_misc_test_env_filter_and_verbose)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds saved_filter = env_save("TEST_FILTER");
	setenv("TEST_FILTER", "from_env", 1);
	ASSERT(write_test_project(&fx, nullptr, "tproj", nullptr), "project");

	sds  out = nullptr;
	i64  ret = run_test_cmd("ignored", true, &out);
	bool ran = file_has("runner.args", "from_env\n");
	env_restore("TEST_FILTER", saved_filter);
	fixture_leave(&fx);

	ASSERT(ret == 0, "test run should succeed");
	ASSERT(ran, "TEST_FILTER wins over the argument");
	ASSERT(has(out, "Running: build/debug/tproj-tests from_env"), "verbose run line");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_test_manifest_sources_and_target_dir)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds saved_filter = env_save("TEST_FILTER");
	unsetenv("TEST_FILTER");
	ASSERT(write_test_project(&fx, nullptr, "tproj", "\n[test]\nsources = [\"tests/t1.c\"]\n"), "project");

	options opt     = { .inputs = (char *[]){ "test" }, .inputs_num = 1, .target_dir = "out/dbg" };
	sds     out     = nullptr;
	i64     ret     = run_captured(handle_test, &opt, &out);
	sds     cc_args = read_file("cc.args");
	bool    built   = file_exists("out/dbg/tproj-tests");
	env_restore("TEST_FILTER", saved_filter);
	fixture_leave(&fx);

	ASSERT(ret == 0, "test run should succeed");
	ASSERT(built, "runner built in --target-dir");
	ASSERT(has(cc_args, "tests/t1.c\n") && !has(cc_args, "tests/t2.c"), "only [test] sources compiled");
	sdsfree(out);
	sdsfree(cc_args);
	PASS();
}

TEST(cov_misc_test_dependency_flags)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds saved_filter = env_save("TEST_FILTER");
	unsetenv("TEST_FILTER");
	ASSERT(write_test_project(&fx, "dependencies = [\"libx\"]\n", "tproj", nullptr), "project");
	make_dirs("deps", "deps/libx", "deps/libx/include");

	sds out     = nullptr;
	i64 ret     = run_test_cmd(nullptr, false, &out);
	sds cc_args = read_file("cc.args");
	env_restore("TEST_FILTER", saved_filter);
	fixture_leave(&fx);

	ASSERT(ret == 0, "test run should succeed");
	ASSERT(has(cc_args, "-Ideps/libx/include"), "dependency include flag passed to the compiler");
	sdsfree(out);
	sdsfree(cc_args);
	PASS();
}

TEST(cov_misc_test_unresolvable_graph_still_builds)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds saved_filter = env_save("TEST_FILTER");
	unsetenv("TEST_FILTER");
	ASSERT(write_test_project(&fx, nullptr, "bad name", nullptr), "project");

	sds out = nullptr;
	i64 ret = run_test_cmd(nullptr, false, &out);
	env_restore("TEST_FILTER", saved_filter);
	fixture_leave(&fx);

	ASSERT(ret == 0, "test run should succeed");
	ASSERT(has(out, "Warning: Could not resolve dependency graph"), "warning printed");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_test_compile_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_test_project(&fx, nullptr, "tproj", nullptr), "project");
	ASSERT(fake_tool(&fx, "fakecc", "exit 1"), "failing compiler");

	sds out = nullptr;
	i64 ret = run_test_cmd(nullptr, false, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "compile failure returns 1");
	ASSERT(has(out, "Failed to compile test runner"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_test_runner_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	sds saved_filter = env_save("TEST_FILTER");
	unsetenv("TEST_FILTER");
	ASSERT(write_test_project(&fx, nullptr, "tproj", nullptr), "project");
	ASSERT(write_file("runner.tpl", "#!/bin/sh\nexit 1\n"), "failing runner");

	sds out = nullptr;
	i64 ret = run_test_cmd(nullptr, false, &out);
	env_restore("TEST_FILTER", saved_filter);
	fixture_leave(&fx);

	ASSERT(ret == 1, "failing test runner returns 1");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_test_no_project_sources)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "tproj", nullptr), "manifest");
	make_dirs("src", "tests", nullptr);
	ASSERT(write_file("tests/t1.c", "int t1;\n"), "test source");

	sds out = nullptr;
	i64 ret = run_test_cmd(nullptr, false, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "no project sources returns 1");
	ASSERT(has(out, "No project sources found in src/*.c"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_test_cannot_create_output_directory)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_test_project(&fx, nullptr, "tproj", nullptr), "project");
	ASSERT(write_file("blocker", "a file\n"), "blocker");

	options opt = { .inputs = (char *[]){ "test" }, .inputs_num = 1, .target_dir = "blocker/sub" };
	sds     out = nullptr;
	i64     ret = run_captured(handle_test, &opt, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "unusable output directory returns 1");
	ASSERT(has(out, "Could not create output directory blocker/sub"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_test_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_test_cmd(nullptr, false, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse Coffee.toml"), "error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* tree                                                                */
/* ------------------------------------------------------------------ */

static bool write_tree_project(void)
{
	make_dirs("deps", "deps/a", "deps/b");
	make_dirs("deps/c", "deps/d", "deps/e");
	make_dirs("deps/z", nullptr, nullptr);
	return write_manifest(nullptr, "treepkg", "\n[dependencies]\na = \"1.2\"\nb = \"1.0\"\n") &&
	       write_file("deps/a/library.toml", "[dependencies]\nc = \"2.0\"\n") &&
	       write_file("deps/b/library.toml", "[") &&
	       write_file("deps/c/library.toml", "[dependencies]\nd = \"3.0\"\n") &&
	       write_file("deps/d/library.toml", "[dependencies]\ne = \"4.0\"\n") &&
	       write_file("deps/e/library.toml", "[dependencies]\nz = \"5.0\"\n") && write_file("deps/z/library.toml", "[");
}

TEST(cov_misc_tree_prints_transitive_dependencies)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_tree_project(), "project");

	sds out = nullptr;
	i64 ret = run_plain(handle_tree, "tree", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "tree should succeed");
	ASSERT(has(out, "treepkg v1.2.3\n"), "root line");
	ASSERT(has(out, "├── a v1.2\n│   └── c v2.0\n│       └── d v3.0\n│           └── e v4.0\n"), "nested chain");
	ASSERT(has(out, "└── b v1.0\n"), "last top-level dependency");
	ASSERT(!has(out, "z v5.0"), "recursion stops at the depth limit");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_tree_uses_lockfile_path)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest(nullptr, "treepkg", "\n[dependencies]\na = \"1.2\"\n"), "manifest");
	make_dirs("store", "deps", "deps/z");
	ASSERT(write_file("store/library.toml", "[dependencies]\nz = \"9.0\"\n"), "locked library.toml");
	sds lock = sdscatprintf(
	    sdsempty(), "version = 1\n\n[[dependencies]]\nname = \"a\"\nversion = \"1.2\"\npath = \"%s/store\"\n", fx.work);
	ASSERT(write_file("Coffee.lock", lock), "lockfile");
	sdsfree(lock);

	sds out = nullptr;
	i64 ret = run_plain(handle_tree, "tree", &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "tree should succeed");
	ASSERT(has(out, "└── a v1.2\n    └── z v9.0\n"), "children read from the locked path");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_tree_broken_manifest)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_broken_manifest(), "manifest");

	sds out = nullptr;
	i64 ret = run_plain(handle_tree, "tree", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "broken manifest should return 1");
	ASSERT(has(out, "Could not parse manifest"), "error message");
	sdsfree(out);
	PASS();
}

/* ------------------------------------------------------------------ */
/* Regression tests for fixes in fix/lint/config/metadata/test/new     */
/* ------------------------------------------------------------------ */

static size_t count_occurrences(const char *haystack, const char *needle)
{
	size_t count = 0;
	if (haystack == nullptr) {
		return 0;
	}
	for (const char *p = strstr(haystack, needle); p != nullptr; p = strstr(p + strlen(needle), needle)) {
		count++;
	}
	return count;
}

/* Project with src/main.c and tests/t1.c; the real find runs a fake clang-tidy. */
static bool write_tidy_project(const struct fixture *fx, const char *pkg, const char *tidy_body)
{
	return write_manifest(nullptr, pkg, nullptr) && write_sources() && fake_tool(fx, "clang-tidy", tidy_body);
}

TEST(cov_misc_fix_runs_clang_tidy_once_through_real_find)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_tidy_project(&fx, "fixpkg", "exit 0"), "project");

	sds out = nullptr;
	i64 ret = run_plain(handle_fix, "fix", &out);
	sds log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "fix should succeed");
	ASSERT(count_occurrences(log, "clang-tidy ") == 1, "clang-tidy runs once for all files");
	ASSERT(has(log, "clang-tidy --fix --quiet --extra-arg=-Isrc --extra-arg=-Iinclude --extra-arg=-I. "
	                "--extra-arg=-Ideps --extra-arg=-Iinclude/fixpkg "),
	       "flags are passed as --extra-arg before the files");
	ASSERT(!has(log, " -- "), "no -- separator");
	ASSERT(has(log, "src/main.c") && has(log, "tests/t1.c"), "real source files are passed");
	ASSERT(!has(log, "{}"), "placeholder is replaced by find");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_fix_returns_one_when_clang_tidy_fails)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_tidy_project(&fx, "fixpkg", "exit 1"), "project");

	sds out = nullptr;
	i64 ret = run_plain(handle_fix, "fix", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "a failing clang-tidy fails fix");
	ASSERT(has(out, "Automatic fix failed"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_fix_without_manifest_uses_default_flags)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_file("Coffee.toml", "[package\nname = \n"), "broken manifest");
	ASSERT(write_sources() && fake_tool(&fx, "clang-tidy", "exit 0"), "sources");

	sds out = nullptr;
	i64 ret = run_plain(handle_fix, "fix", &out);
	sds log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "fix succeeds with default flags");
	ASSERT(has(log, "clang-tidy --fix --quiet --extra-arg=-Isrc src/") ||
	           has(log, "clang-tidy --fix --quiet --extra-arg=-Isrc tests/"),
	       "only -Isrc is passed");
	ASSERT(!has(log, "-Iinclude"), "no manifest-derived flags");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_lint_runs_clang_tidy_once_without_fix)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_tidy_project(&fx, "lintpkg", "exit 0"), "project");

	sds out = nullptr;
	i64 ret = run_plain(handle_lint, "lint", &out);
	sds log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "lint should succeed");
	ASSERT(count_occurrences(log, "clang-tidy ") == 1, "clang-tidy runs once for all files");
	ASSERT(has(log, "clang-tidy --quiet --extra-arg=-Isrc --extra-arg=-Iinclude --extra-arg=-I. "
	                "--extra-arg=-Ideps --extra-arg=-Iinclude/lintpkg "),
	       "flags are passed as --extra-arg before the files");
	ASSERT(!has(log, "--fix"), "lint without --fix does not fix");
	ASSERT(has(log, "src/main.c") && has(log, "tests/t1.c"), "real source files are passed");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_lint_fix_forwards_fix_to_real_find)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_tidy_project(&fx, "lintpkg", "exit 0"), "project");

	options opt = { .inputs = (char *[]){ "lint" }, .inputs_num = 1, .fix = true };
	sds     out = nullptr;
	i64     ret = run_captured(handle_lint, &opt, &out);
	sds     log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "lint --fix should succeed");
	ASSERT(has(log, "clang-tidy --fix --quiet --extra-arg=-Isrc "), "--fix precedes --quiet");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_lint_returns_one_when_clang_tidy_fails)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_tidy_project(&fx, "lintpkg", "exit 1"), "project");

	sds out = nullptr;
	i64 ret = run_plain(handle_lint, "lint", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "a failing clang-tidy fails lint");
	ASSERT(has(out, "Linting failed"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_lint_without_manifest_uses_default_flags)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_file("Coffee.toml", "[package\nname = \n"), "broken manifest");
	ASSERT(write_sources() && fake_tool(&fx, "clang-tidy", "exit 0"), "sources");

	sds out = nullptr;
	i64 ret = run_plain(handle_lint, "lint", &out);
	sds log = tool_log(&fx);
	fixture_leave(&fx);

	ASSERT(ret == 0, "lint succeeds with default flags");
	ASSERT(has(log, "--quiet --extra-arg=-Isrc ") && !has(log, "-Iinclude"), "only -Isrc is passed");
	sdsfree(out);
	sdsfree(log);
	PASS();
}

TEST(cov_misc_append_clang_tidy_extra_args_edge_cases)
{
	char *argv[8] = { nullptr };

	size_t empty_end = append_clang_tidy_extra_args("", argv, 3);
	size_t null_end  = append_clang_tidy_extra_args(nullptr, argv, 3);
	ASSERT(empty_end == 3 && null_end == 3, "no flags append nothing");

	size_t end = append_clang_tidy_extra_args("-Isrc  -DX=1 -I\"a b\"", argv, 1);
	ASSERT(end == 4, "three tokens are appended after index 1");
	ASSERT(strcmp(argv[1], "--extra-arg=-Isrc") == 0, "plain flag");
	ASSERT(strcmp(argv[2], "--extra-arg=-DX=1") == 0, "repeated spaces are skipped");
	ASSERT(strcmp(argv[3], "--extra-arg=-Ia b") == 0, "quotes are stripped and the space kept");
	for (size_t i = 1; i < end; i++) {
		sdsfree(argv[i]);
	}
	PASS();
}

TEST(cov_misc_config_list_reads_tables_after_top_level_keys)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("alpha = \"a\"\nbeta = \"b\"\n[one]\nk1 = \"v1\"\nk2 = \"v2\"\n"
	                    "[two]\nk3 = \"v3\"\n[three.sub]\nk4 = \"v4\"\n"),
	       "config");

	sds out = nullptr;
	i64 ret = run_config("list", nullptr, nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "list succeeds");
	ASSERT(has(out, "alpha = \"a\"\n") && has(out, "beta = \"b\"\n"), "top-level keys");
	ASSERT(has(out, "one.k1 = \"v1\"\n") && has(out, "one.k2 = \"v2\"\n"), "first section keys");
	ASSERT(has(out, "two.k3 = \"v3\"\n"), "second section key");
	ASSERT(!has(out, "(empty config)"), "config is not reported empty");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_list_with_arrays_and_sections)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_config("name = \"n\"\nlist = [\"x\", \"y\"]\nother = [1, 2]\n[sec]\nk = \"v\"\n[sec2]\nj = \"w\"\n"),
	       "config");

	sds out = nullptr;
	i64 ret = run_config("--list", nullptr, nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 0, "list succeeds");
	ASSERT(has(out, "name = \"n\"\n"), "top-level key");
	ASSERT(has(out, "sec.k = \"v\"\n"), "section after arrays is listed");
	ASSERT(has(out, "sec2.j = \"w\"\n"), "second section after arrays is listed");
	ASSERT(!has(out, "list"), "arrays are not misread as sections");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_config_unset_without_config_file)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	sds out = nullptr;
	i64 ret = run_config("unset", "k", nullptr, &out);
	sds cfg = read_config();
	fixture_leave(&fx);

	ASSERT(ret == 0, "unset without a config file succeeds");
	ASSERT(cfg == nullptr, "no config file is created");
	sdsfree(out);
	PASS();
}

/* True if `python3 -I -c <script> <file>` exits with 0. */
static bool python_script_accepts(const char *script, const char *file)
{
	char *argv[] = { "python3", "-I", "-c", unconst(script), unconst(file), nullptr };
	return run_command(argv, 0) == 0;
}

static bool python_available(void)
{
	return python_script_accepts("pass", "unused");
}

/* Save the JSON document inside out, dropping warnings that share the stream. */
static bool write_json_part(const char *path, const char *out)
{
	const char *start = strstr(out, "{\n");
	if (start == nullptr) {
		return false;
	}
	return write_file(path, start);
}

static const char *json_loads_script = "import json,sys; json.load(open(sys.argv[1]))";
static const char *json_check_script =
    "import json,sys; d=json.load(open(sys.argv[1])); p=d['package']; "
    "assert p['description']=='q\"uote \\\\ back\\nnl\\ttab\\rcr \\x01 end', repr(p['description']); "
    "assert d['dependencies']==['we\"ird\\\\dep', 'plain'], d['dependencies']";

TEST(cov_misc_metadata_escapes_json_strings)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_file("Coffee.toml", "dependencies = [\"we\\\"ird\\\\dep\", \"plain\"]\n\n"
	                                 "[package]\nname = \"metaesc\"\nversion = \"1.0\"\nedition = \"c23\"\n"
	                                 "description = \"q\\\"uote \\\\ back\\nnl\\ttab\\rcr \\u0001 end\"\n"
	                                 "license = \"MIT\"\n"),
	       "manifest");

	sds  out   = nullptr;
	i64  ret   = run_plain(handle_metadata, "metadata", &out);
	bool wrote = write_json_part("metadata.json", out);
	bool valid = !python_available() || python_script_accepts(json_loads_script, "metadata.json");
	bool exact = !python_available() || python_script_accepts(json_check_script, "metadata.json");
	fixture_leave(&fx);

	ASSERT(ret == 0, "metadata succeeds");
	ASSERT(has(out, "\"description\": \"q\\\"uote \\\\ back\\nnl\\ttab\\rcr \\u0001 end\","), "escaped description");
	ASSERT(has(out, "\"we\\\"ird\\\\dep\""), "escaped dependency name");
	ASSERT(wrote && valid, "output is valid JSON");
	ASSERT(exact, "parsed JSON values equal the manifest values");
	ASSERT(!has(out, "\n\t"), "no raw tab leaked into the output");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_metadata_plain_dependencies_are_valid_json)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_manifest("dependencies = [\"liba\", \"libb\"]\n\n", "metaplain", nullptr), "manifest");
	make_dirs("deps", "deps/liba", "deps/libb");

	sds  out   = nullptr;
	i64  ret   = run_plain(handle_metadata, "metadata", &out);
	bool wrote = write_json_part("metadata.json", out);
	bool valid = !python_available() || python_script_accepts(json_loads_script, "metadata.json");
	fixture_leave(&fx);

	ASSERT(ret == 0, "metadata succeeds");
	ASSERT(has(out, "\"liba\",\n    \"libb\"\n  ]"), "dependencies are comma separated strings");
	ASSERT(wrote && valid, "output is valid JSON");
	sdsfree(out);
	PASS();
}

static i64 run_test_with_runner_status(int status, sds *out)
{
	sds saved_filter = env_save("TEST_FILTER");
	unsetenv("TEST_FILTER");
	sds  script = sdscatprintf(sdsempty(), "#!/bin/sh\nexit %d\n", status);
	bool ok     = write_file("runner.tpl", script);
	sdsfree(script);
	i64 ret = ok ? run_test_cmd(nullptr, false, out) : -1;
	env_restore("TEST_FILTER", saved_filter);
	return ret;
}

TEST(cov_misc_test_returns_runner_exit_status)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");
	ASSERT(write_test_project(&fx, nullptr, "tproj", nullptr), "project");

	sds pass_out = nullptr;
	sds fail_out = nullptr;
	i64 pass_ret = run_test_with_runner_status(0, &pass_out);
	i64 fail_ret = run_test_with_runner_status(3, &fail_out);
	fixture_leave(&fx);

	ASSERT(pass_ret == 0, "runner exiting 0 returns 0");
	ASSERT(fail_ret == 3, "runner exiting 3 returns 3");
	sdsfree(pass_out);
	sdsfree(fail_out);
	PASS();
}

/* ---- handle_new error paths ---- */

struct new_blocker {
	const char *path;   /* relative to the project directory */
	const char *target; /* symlink target; nullptr: directory in place of a file */
	const char *message;
	bool        lib;
};

#define NEW_NO_TARGET nullptr

static const struct new_blocker new_blockers[] = {
	{ "include", "no-such-target", "Could not create include directory", false },
	{ "src", "no-such-target", "Could not create src directory", false },
	{ "deps", "no-such-target", "Could not create deps directory", false },
	{ "tests", "no-such-target", "Could not create tests directory", false },
	{ "doc", "no-such-target", "Could not create doc directory", false },
	{ "scripts", "no-such-target", "Could not create scripts directory", false },
	{ "build", "no-such-target", "Could not create build directory", false },
	{ "include/blk", "no-such-target", "Could not create include/blk directory", false },
	{ ".gitignore", NEW_NO_TARGET, "Could not create .gitignore", false },
	{ "LICENSE", NEW_NO_TARGET, "Could not create LICENSE", false },
	{ "README.md", NEW_NO_TARGET, "Could not create README.md", false },
	{ "Makefile", NEW_NO_TARGET, "Could not create Makefile", false },
	{ "Coffee.toml", "no-such-dir/x", "Could not create Coffee.toml", false },
	{ "src/main.c", NEW_NO_TARGET, "Could not create main.c", false },
	{ "include/blk/blk.h", NEW_NO_TARGET, "Could not create include/blk/blk.h", false },
	{ "src/lib.c", NEW_NO_TARGET, "Could not create lib.c", true },
};

static const char *const new_subdirs[] = {
	"include", "src", "deps", "tests", "doc", "scripts", "build", "include/blk",
};

static bool place_new_blocker(const struct new_blocker *blocker)
{
	if (mkdir("blk", 0755) != 0) {
		return false;
	}
	/* Parent directories of the blocker must exist; the blocker itself is
	 * a dangling symlink or a directory. */
	for (size_t i = 0; i < sizeof(new_subdirs) / sizeof(new_subdirs[0]); i++) {
		sds dir = sdscatprintf(sdsempty(), "blk/%s", new_subdirs[i]);
		if (strcmp(new_subdirs[i], blocker->path) != 0 &&
		    strncmp(blocker->path, new_subdirs[i], strlen(new_subdirs[i])) == 0) {
			mkdir(dir, 0755);
		}
		sdsfree(dir);
	}
	sds  path = sdscatprintf(sdsempty(), "blk/%s", blocker->path);
	bool ok   = blocker->target != nullptr ? symlink(blocker->target, path) == 0 : mkdir(path, 0755) == 0;
	sdsfree(path);
	return ok;
}

TEST(cov_misc_new_reports_each_creation_failure)
{
	for (size_t i = 0; i < sizeof(new_blockers) / sizeof(new_blockers[0]); i++) {
		struct fixture fx;
		ASSERT(fixture_enter(&fx), "fixture");
		bool placed = place_new_blocker(&new_blockers[i]);

		char   *inputs[] = { "new", "blk" };
		options opt      = { .inputs = inputs, .inputs_num = 2, .lib = new_blockers[i].lib };
		sds     out      = nullptr;
		i64     ret      = run_captured(handle_new, &opt, &out);
		bool    matches  = ret == 1 && has(out, new_blockers[i].message);
		if (!matches) {
			printf("  blocker %s: ret=%lld output=%s\n", new_blockers[i].path, (long long)ret, out);
		}
		sdsfree(out);
		fixture_leave(&fx);

		ASSERT(placed, "blocker placed");
		ASSERT(matches, "new reports the blocked step");
	}
	PASS();
}

/* Create the standard project layout so that only files remain to be created. */
static void make_new_layout(void)
{
	mkdir("blk", 0755);
	for (size_t i = 0; i < sizeof(new_subdirs) / sizeof(new_subdirs[0]); i++) {
		sds dir = sdscatprintf(sdsempty(), "blk/%s", new_subdirs[i]);
		mkdir(dir, 0755);
		sdsfree(dir);
	}
}

/*
 * With umask 0277 every file created by handle_new is read-only, so the
 * second creation of the same path (the library variants of Makefile and
 * Coffee.toml overwrite the plain ones) fails.  Directories are made first
 * because they would be created read-only too.
 */
static i64 run_new_lib_with_readonly_files(const char *precreated_file, sds *out)
{
	make_new_layout();
	if (precreated_file != nullptr && !write_file(precreated_file, "")) {
		return -1;
	}
	char   *inputs[] = { "new", "blk" };
	options opt      = { .inputs = inputs, .inputs_num = 2, .lib = true };
	mode_t  old_mask = umask(0277);
	i64     ret      = run_captured(handle_new, &opt, out);
	umask(old_mask);
	return ret;
}

TEST(cov_misc_new_lib_makefile_creation_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	sds out = nullptr;
	i64 ret = run_new_lib_with_readonly_files(nullptr, &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "library Makefile cannot overwrite the read-only Makefile");
	ASSERT(has(out, "Could not create Makefile"), "error message");
	sdsfree(out);
	PASS();
}

TEST(cov_misc_new_lib_manifest_creation_failure)
{
	struct fixture fx;
	ASSERT(fixture_enter(&fx), "fixture");

	/* A writable Makefile survives both writes, so only Coffee.toml fails. */
	sds out = nullptr;
	i64 ret = run_new_lib_with_readonly_files("blk/Makefile", &out);
	fixture_leave(&fx);

	ASSERT(ret == 1, "library Coffee.toml cannot overwrite the read-only manifest");
	ASSERT(has(out, "Could not create Coffee.toml"), "error message");
	sdsfree(out);
	PASS();
}

void coffee_register_cov_misc_commands_tests(void)
{
	TEST_REGISTER(cov_misc_bench_legacy_runs_make_target);
	TEST_REGISTER(cov_misc_bench_verbose_prints_command);
	TEST_REGISTER(cov_misc_bench_make_failure);
	TEST_REGISTER(cov_misc_bench_broken_manifest);
	TEST_REGISTER(cov_misc_cflags_from_manifest_dependencies);
	TEST_REGISTER(cov_misc_cflags_single_package);
	TEST_REGISTER(cov_misc_cflags_broken_manifest);
	TEST_REGISTER(cov_misc_libs_from_manifest_dependencies);
	TEST_REGISTER(cov_misc_libs_single_package);
	TEST_REGISTER(cov_misc_libs_broken_manifest);
	TEST_REGISTER(cov_misc_check_reports_warnings_and_runs_compiler);
	TEST_REGISTER(cov_misc_check_compiler_failure);
	TEST_REGISTER(cov_misc_clean_rm_failure);
	TEST_REGISTER(cov_misc_config_list_prints_top_level_keys);
	TEST_REGISTER(cov_misc_config_list_prints_section_keys);
	TEST_REGISTER(cov_misc_config_list_invalid_file);
	TEST_REGISTER(cov_misc_config_get_integer_value);
	TEST_REGISTER(cov_misc_config_get_missing_key);
	TEST_REGISTER(cov_misc_config_get_without_config_file);
	TEST_REGISTER(cov_misc_config_set_inserts_before_next_section);
	TEST_REGISTER(cov_misc_config_set_appends_to_last_section);
	TEST_REGISTER(cov_misc_config_set_and_unset_unwritable_file);
	TEST_REGISTER(cov_misc_config_usage_errors);
	TEST_REGISTER(cov_misc_doc_check_found_and_missing);
	TEST_REGISTER(cov_misc_doc_unknown_subcommand);
	TEST_REGISTER(cov_misc_doc_manifest_errors);
	TEST_REGISTER(cov_misc_doc_generate_doxyfile_from_doc_section);
	TEST_REGISTER(cov_misc_doc_generate_defaults_for_empty_doc_section);
	TEST_REGISTER(cov_misc_doc_generate_unwritable_doxyfile);
	TEST_REGISTER(cov_misc_locate_project_path_longer_than_path_max);
	TEST_REGISTER(cov_misc_init_skips_unreadable_c_files);
	TEST_REGISTER(cov_misc_fix_verbose_success);
	TEST_REGISTER(cov_misc_lint_fix_verbose_success);
	TEST_REGISTER(cov_misc_fmt_formatter_failure);
	TEST_REGISTER(cov_misc_grep_pattern_not_found);
	TEST_REGISTER(cov_misc_info_prints_license_and_dependencies);
	TEST_REGISTER(cov_misc_info_broken_manifest);
	TEST_REGISTER(cov_misc_pkgid_broken_manifest);
	TEST_REGISTER(cov_misc_list_installed_without_bin_dir);
	TEST_REGISTER(cov_misc_list_installed_bin_is_not_a_directory);
	TEST_REGISTER(cov_misc_list_installed_empty_and_populated);
	TEST_REGISTER(cov_misc_list_project_dependencies);
	TEST_REGISTER(cov_misc_list_broken_manifest);
	TEST_REGISTER(cov_misc_machete_reports_unused_dependencies);
	TEST_REGISTER(cov_misc_machete_all_dependencies_used);
	TEST_REGISTER(cov_misc_machete_broken_manifest);
	TEST_REGISTER(cov_misc_metadata_features_and_transitive_dependencies);
	TEST_REGISTER(cov_misc_metadata_unresolvable_graph);
	TEST_REGISTER(cov_misc_metadata_broken_manifest);
	TEST_REGISTER(cov_misc_new_cannot_create_directory);
	TEST_REGISTER(cov_misc_new_refuses_existing_project);
	TEST_REGISTER(cov_misc_new_trailing_slash_uses_default_name);
	TEST_REGISTER(cov_misc_init_reports_each_creation_failure);
	TEST_REGISTER(cov_misc_init_in_removed_directory_falls_back_to_default_name);
	TEST_REGISTER(cov_misc_package_creates_tarball);
	TEST_REGISTER(cov_misc_package_tar_failure);
	TEST_REGISTER(cov_misc_package_broken_manifest);
	TEST_REGISTER(cov_misc_report_deps_classifies_sources);
	TEST_REGISTER(cov_misc_report_unknown_type);
	TEST_REGISTER(cov_misc_report_broken_manifest);
	TEST_REGISTER(cov_misc_report_audit_unresolvable_graph);
	TEST_REGISTER(cov_misc_report_audit_up_to_date_and_path);
	TEST_REGISTER(cov_misc_report_audit_outdated);
	TEST_REGISTER(cov_misc_report_audit_fetch_failure);
	TEST_REGISTER(cov_misc_run_executable_missing);
	TEST_REGISTER(cov_misc_run_forwards_arguments);
	TEST_REGISTER(cov_misc_search_lists_matching_packages);
	TEST_REGISTER(cov_misc_search_empty_index);
	TEST_REGISTER(cov_misc_test_builds_and_runs_with_filter);
	TEST_REGISTER(cov_misc_test_env_filter_and_verbose);
	TEST_REGISTER(cov_misc_test_manifest_sources_and_target_dir);
	TEST_REGISTER(cov_misc_test_dependency_flags);
	TEST_REGISTER(cov_misc_test_unresolvable_graph_still_builds);
	TEST_REGISTER(cov_misc_test_compile_failure);
	TEST_REGISTER(cov_misc_test_runner_failure);
	TEST_REGISTER(cov_misc_test_no_project_sources);
	TEST_REGISTER(cov_misc_test_cannot_create_output_directory);
	TEST_REGISTER(cov_misc_test_broken_manifest);
	TEST_REGISTER(cov_misc_tree_prints_transitive_dependencies);
	TEST_REGISTER(cov_misc_tree_uses_lockfile_path);
	TEST_REGISTER(cov_misc_tree_broken_manifest);
	TEST_REGISTER(cov_misc_fix_runs_clang_tidy_once_through_real_find);
	TEST_REGISTER(cov_misc_fix_returns_one_when_clang_tidy_fails);
	TEST_REGISTER(cov_misc_fix_without_manifest_uses_default_flags);
	TEST_REGISTER(cov_misc_lint_runs_clang_tidy_once_without_fix);
	TEST_REGISTER(cov_misc_lint_fix_forwards_fix_to_real_find);
	TEST_REGISTER(cov_misc_lint_returns_one_when_clang_tidy_fails);
	TEST_REGISTER(cov_misc_lint_without_manifest_uses_default_flags);
	TEST_REGISTER(cov_misc_append_clang_tidy_extra_args_edge_cases);
	TEST_REGISTER(cov_misc_config_list_reads_tables_after_top_level_keys);
	TEST_REGISTER(cov_misc_config_list_with_arrays_and_sections);
	TEST_REGISTER(cov_misc_config_unset_without_config_file);
	TEST_REGISTER(cov_misc_metadata_escapes_json_strings);
	TEST_REGISTER(cov_misc_metadata_plain_dependencies_are_valid_json);
	TEST_REGISTER(cov_misc_test_returns_runner_exit_status);
	TEST_REGISTER(cov_misc_new_reports_each_creation_failure);
	TEST_REGISTER(cov_misc_new_lib_makefile_creation_failure);
	TEST_REGISTER(cov_misc_new_lib_manifest_creation_failure);
}
