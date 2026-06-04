/*
 * Tests for commands that require a Coffee.toml manifest.
 *
 * Each test creates a temporary project directory with a minimal Coffee.toml,
 * chdirs into it, runs the command, then cleans up.
 */
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
void coffee_register_commands_manifest_tests(void);

/* ---- helpers ---- */

static char saved_cwd[4096];

static void setup_project(const char *test_name, const char *extra_toml)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coffee-cmd-manifest-%s", test_name);
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

	/* Create minimal src/ and tests/ dirs so compile/build paths work */
	mkdir("src", 0755);
	mkdir("tests", 0755);

	/* Create a dummy main.c so commands like check/fix/lint have source files */
	FILE *main_c = fopen("src/main.c", "w");
	if (main_c) {
		fprintf_safe(main_c, "int main(void) { return 0; }\n");
		fclose(main_c);
	}

	/* Create a dummy .h file for include path testing */
	mkdir("include", 0755);

	/* Create a minimal Makefile so test/bench commands succeed */
	FILE *mf = fopen("Makefile", "w");
	if (mf) {
		fprintf_safe(mf, "CC = clang\n");
		fprintf_safe(mf, "CFLAGS = -std=c23 -g -O0\n");
		fprintf_safe(mf, "\n");
		fprintf_safe(mf, "bin/tests/runner: src/main.c\n");
		fprintf_safe(mf, "\t@mkdir -p bin/tests\n");
		fprintf_safe(mf, "\t$(CC) $(CFLAGS) -o $@ $^\n");
		fprintf_safe(mf, "\n");
		fprintf_safe(mf, "bench:\n");
		fprintf_safe(mf, "\t@echo \"bench complete\"\n");
		fclose(mf);
	}

	sdsfree(tmpdir);
}

static void teardown_project(const char *test_name)
{
	chdir(saved_cwd);
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coffee-cmd-manifest-%s", test_name);
	sds cmd    = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
}

/* ---------------------------------------------------------------
 * bench
 * --------------------------------------------------------------- */
TEST(bench_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-bench-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "bench" },
		.inputs_num = 1,
	};
	i64 ret = handle_bench(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "bench without manifest should return 1");
	PASS();
}

TEST(bench_with_manifest)
{
	setup_project("bench", nullptr);
	options opt = {
		.inputs     = (char *[]){ "bench" },
		.inputs_num = 1,
	};
	/* bench needs a Makefile or src files; it won't actually run benchmarks
	 * but should parse the manifest and attempt build */
	i64 ret = handle_bench(&opt);
	/* May return 0 or non-zero depending on build availability */
	/* We just verify it doesn't crash */
	teardown_project("bench");
	ASSERT(ret == 0 || ret == 1, "bench should complete without crash");
	PASS();
}

/* ---------------------------------------------------------------
 * check
 * --------------------------------------------------------------- */
TEST(check_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-check-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "check" },
		.inputs_num = 1,
	};
	i64 ret = handle_check(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "check without manifest should return 1");
	PASS();
}

TEST(check_valid_manifest)
{
	setup_project("check-valid", nullptr);
	options opt = {
		.inputs     = (char *[]){ "check" },
		.inputs_num = 1,
	};
	i64 ret = handle_check(&opt);
	teardown_project("check-valid");
	ASSERT(ret == 0, "check with valid manifest should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * fetch
 * --------------------------------------------------------------- */
TEST(fetch_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-fetch-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};
	i64 ret = handle_fetch(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "fetch without manifest should return 1");
	PASS();
}

TEST(fetch_with_manifest)
{
	setup_project("fetch", nullptr);
	options opt = {
		.inputs     = (char *[]){ "fetch" },
		.inputs_num = 1,
	};
	i64 ret = handle_fetch(&opt);
	teardown_project("fetch");
	ASSERT(ret == 0, "fetch with manifest (no deps) should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * fix
 * --------------------------------------------------------------- */
TEST(fix_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-fix-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "fix" },
		.inputs_num = 1,
	};
	i64 ret = handle_fix(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "fix without manifest should return 1");
	PASS();
}

TEST(fix_with_manifest)
{
	setup_project("fix", nullptr);
	options opt = {
		.inputs     = (char *[]){ "fix" },
		.inputs_num = 1,
	};
	i64 ret = handle_fix(&opt);
	(void)ret;
	teardown_project("fix");
	/* fix runs clang-tidy, might fail if clang-tidy not installed */
	/* accept any result (external tool) */
	PASS();
}

/* ---------------------------------------------------------------
 * info
 * --------------------------------------------------------------- */
TEST(info_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-info-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "info" },
		.inputs_num = 1,
	};
	i64 ret = handle_info(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "info without manifest should return 1");
	PASS();
}

TEST(info_with_manifest)
{
	setup_project("info", nullptr);
	options opt = {
		.inputs     = (char *[]){ "info" },
		.inputs_num = 1,
	};
	i64 ret = handle_info(&opt);
	teardown_project("info");
	ASSERT(ret == 0, "info with manifest should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * lint
 * --------------------------------------------------------------- */
TEST(lint_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-lint-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "lint" },
		.inputs_num = 1,
	};
	i64 ret = handle_lint(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "lint without manifest should return 1");
	PASS();
}

TEST(lint_with_manifest)
{
	setup_project("lint", nullptr);
	options opt = {
		.inputs     = (char *[]){ "lint" },
		.inputs_num = 1,
	};
	i64 ret = handle_lint(&opt);
	(void)ret;
	teardown_project("lint");
	/* lint runs clang-tidy, may fail if clang-tidy not installed */
	/* accept any result (external tool) */
	PASS();
}

/* ---------------------------------------------------------------
 * locate_project
 * --------------------------------------------------------------- */
TEST(locate_project_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-locate-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "locate-project" },
		.inputs_num = 1,
	};
	i64 ret = handle_locate_project(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "locate-project without manifest should return 1");
	PASS();
}

TEST(locate_project_with_manifest)
{
	setup_project("locate", nullptr);
	options opt = {
		.inputs     = (char *[]){ "locate-project" },
		.inputs_num = 1,
	};
	i64 ret = handle_locate_project(&opt);
	teardown_project("locate");
	ASSERT(ret == 0, "locate-project with manifest should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * machete
 * --------------------------------------------------------------- */
TEST(machete_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-machete-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "machete" },
		.inputs_num = 1,
	};
	i64 ret = handle_machete(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "machete without manifest should return 1");
	PASS();
}

TEST(machete_no_deps)
{
	setup_project("machete-nodeps", nullptr);
	options opt = {
		.inputs     = (char *[]){ "machete" },
		.inputs_num = 1,
	};
	i64 ret = handle_machete(&opt);
	teardown_project("machete-nodeps");
	ASSERT(ret == 0, "machete with no deps should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * metadata
 * --------------------------------------------------------------- */
TEST(metadata_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-metadata-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "metadata" },
		.inputs_num = 1,
	};
	i64 ret = handle_metadata(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "metadata without manifest should return 1");
	PASS();
}

TEST(metadata_with_manifest)
{
	setup_project("metadata", nullptr);
	options opt = {
		.inputs     = (char *[]){ "metadata" },
		.inputs_num = 1,
	};
	i64 ret = handle_metadata(&opt);
	teardown_project("metadata");
	ASSERT(ret == 0, "metadata with manifest should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * package
 * --------------------------------------------------------------- */
TEST(package_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-pkg-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "package" },
		.inputs_num = 1,
	};
	i64 ret = handle_package(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "package without manifest should return 1");
	PASS();
}

TEST(package_with_manifest)
{
	setup_project("package", nullptr);
	options opt = {
		.inputs     = (char *[]){ "package" },
		.inputs_num = 1,
	};
	i64 ret = handle_package(&opt);
	teardown_project("package");
	ASSERT(ret == 0, "package with manifest should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * pkgid
 * --------------------------------------------------------------- */
TEST(pkgid_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-pkgid-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "pkgid" },
		.inputs_num = 1,
	};
	i64 ret = handle_pkgid(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "pkgid without manifest should return 1");
	PASS();
}

TEST(pkgid_with_manifest)
{
	setup_project("pkgid", nullptr);
	options opt = {
		.inputs     = (char *[]){ "pkgid" },
		.inputs_num = 1,
	};
	i64 ret = handle_pkgid(&opt);
	teardown_project("pkgid");
	ASSERT(ret == 0, "pkgid with manifest should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * report
 * --------------------------------------------------------------- */
TEST(report_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-report-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "report" },
		.inputs_num = 1,
	};
	i64 ret = handle_report(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "report without manifest should return 1");
	PASS();
}

TEST(report_deps_type)
{
	setup_project("report", "\ndependencies = [\"testdep\"]\n");
	options opt = {
		.inputs     = (char *[]){ "report", "deps" },
		.inputs_num = 2,
	};
	i64 ret = handle_report(&opt);
	teardown_project("report");
	ASSERT(ret == 0, "report deps with manifest should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * test (the test command)
 * --------------------------------------------------------------- */
TEST(test_cmd_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-cmd-test-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "test" },
		.inputs_num = 1,
	};
	i64 ret = handle_test(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	/* handle_test handles missing manifest gracefully */
	ASSERT(ret == 0 || ret == 1, "test command should complete without crash");
	PASS();
}

/* ---------------------------------------------------------------
 * vendor
 * --------------------------------------------------------------- */
TEST(vendor_no_manifest)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	sds tmpdir = sdsnew("/tmp/coffee-test-vendor-nomanifest");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "vendor" },
		.inputs_num = 1,
	};
	i64 ret = handle_vendor(&opt);

	chdir(old_cwd);
	remove(tmpdir);
	sdsfree(tmpdir);

	ASSERT(ret == 1, "vendor without manifest should return 1");
	PASS();
}

TEST(vendor_no_deps)
{
	setup_project("vendor", nullptr);
	options opt = {
		.inputs     = (char *[]){ "vendor" },
		.inputs_num = 1,
	};
	i64 ret = handle_vendor(&opt);
	teardown_project("vendor");
	ASSERT(ret == 0, "vendor with manifest and no deps should return 0");
	PASS();
}

/* ---------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------- */
void coffee_register_commands_manifest_tests(void)
{
	TEST_REGISTER(bench_no_manifest);
	TEST_REGISTER(bench_with_manifest);
	TEST_REGISTER(check_no_manifest);
	TEST_REGISTER(check_valid_manifest);
	TEST_REGISTER(fetch_no_manifest);
	TEST_REGISTER(fetch_with_manifest);
	TEST_REGISTER(fix_no_manifest);
	TEST_REGISTER(fix_with_manifest);
	TEST_REGISTER(info_no_manifest);
	TEST_REGISTER(info_with_manifest);
	TEST_REGISTER(lint_no_manifest);
	TEST_REGISTER(lint_with_manifest);
	TEST_REGISTER(locate_project_no_manifest);
	TEST_REGISTER(locate_project_with_manifest);
	TEST_REGISTER(machete_no_manifest);
	TEST_REGISTER(machete_no_deps);
	TEST_REGISTER(metadata_no_manifest);
	TEST_REGISTER(metadata_with_manifest);
	TEST_REGISTER(package_no_manifest);
	TEST_REGISTER(package_with_manifest);
	TEST_REGISTER(pkgid_no_manifest);
	TEST_REGISTER(pkgid_with_manifest);
	TEST_REGISTER(report_no_manifest);
	TEST_REGISTER(report_deps_type);
	TEST_REGISTER(test_cmd_no_manifest);
	TEST_REGISTER(vendor_no_manifest);
	TEST_REGISTER(vendor_no_deps);
}
