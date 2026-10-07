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

static char saved_cwd[4'096];

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
	/* These fixtures hand-write their Makefile, so opt out of generation */
	fprintf_safe(fp, "\n[compilation]\nMakefile = \"legacy\"\n");
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
	test_remove_tree(tmpdir);
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
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	fprintf_safe(fp, "TARGET := build/run\n");
	fprintf_safe(fp, "all: $(TARGET)\n");
	fprintf_safe(fp, "$(TARGET): src/main.c\n");
	fprintf_safe(fp, "\t@mkdir -p build\n");
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

TEST(run_from_subdirectory_generated)
{
	setup_project("run-subdir-gen", nullptr);

	/* Drop the legacy opt-in: this project uses the generated Makefile */
	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"run-subdir-gen\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);
	remove("Makefile");

	ASSERT(chdir("src") == 0, "chdir src failed");
	options opt = {
		.inputs     = (char *[]){ "run" },
		.inputs_num = 1,
	};
	i64 ret = handle_run(&opt);
	ASSERT(ret == 0, "run from subdirectory (generated) should succeed");
	teardown_project("run-subdir-gen");
	PASS();
}

TEST(run_from_subdirectory_legacy)
{
	setup_project("run-subdir-legacy", nullptr);

	/* Legacy Makefile that builds the binary */
	FILE *fp = fopen("Makefile", "w");
	ASSERT(fp != nullptr, "could not create Makefile");
	fprintf_safe(fp, "CC := clang\n");
	fprintf_safe(fp, "CFLAGS := -std=c23 -O0 -g\n");
	fprintf_safe(fp, "TARGET := build/run-subdir-legacy\n");
	fprintf_safe(fp, "all: $(TARGET)\n");
	fprintf_safe(fp, "$(TARGET): src/main.c\n");
	fprintf_safe(fp, "\t@mkdir -p build\n");
	fprintf_safe(fp, "\t$(CC) $(CFLAGS) -o $(TARGET) src/main.c\n");
	fclose(fp);

	ASSERT(chdir("src") == 0, "chdir src failed");
	options opt = {
		.inputs     = (char *[]){ "run" },
		.inputs_num = 1,
	};
	i64 ret = handle_run(&opt);
	ASSERT(ret == 0, "run from subdirectory (legacy) should succeed");
	teardown_project("run-subdir-legacy");
	PASS();
}

/* ---------------------------------------------------------------
 * build: generated Makefile
 * --------------------------------------------------------------- */
TEST(build_generates_makefile)
{
	setup_project("build-gen", nullptr);

	/* Drop the legacy opt-in: this project uses the generated Makefile */
	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"build-gen\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);
	remove("Makefile");

	options opt = {
		.inputs     = (char *[]){ "build" },
		.inputs_num = 1,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0, "build should generate Makefile and succeed");
	ASSERT(access("Makefile", F_OK) == 0, "Makefile should be generated");
	ASSERT(access("build/build-gen", X_OK) == 0, "binary should be built");
	teardown_project("build-gen");
	PASS();
}

TEST(build_target_argument)
{
	setup_project("build-tgt", nullptr);

	/* Legacy Makefile with a sentinel test target */
	FILE *fp = fopen("Makefile", "w");
	ASSERT(fp != nullptr, "could not create Makefile");
	fprintf_safe(fp, "all:\n\t@echo 'all target'\n");
	fprintf_safe(fp, "test:\n\ttouch test-ran\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "build", "test" },
		.inputs_num = 2,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0, "build test should succeed");
	ASSERT(access("test-ran", F_OK) == 0, "test target should have run");
	remove("test-ran");
	teardown_project("build-tgt");
	PASS();
}

TEST(build_legacy_requires_makefile)
{
	setup_project("build-legacy", nullptr);
	remove("Makefile");

	options opt = {
		.inputs     = (char *[]){ "build" },
		.inputs_num = 1,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 1, "legacy build without Makefile should fail");
	teardown_project("build-legacy");
	PASS();
}

TEST(build_all_flags_legacy)
{
	/* Every legacy flag at once: -j, RELEASE, DEBUG, CFLAGS_EXTRA and a
	 * target.  This used to overflow the fixed make_argv array. */
	setup_project("build-allflags", "[features]\nextra = []\n");

	/* Legacy Makefile with a sentinel test target */
	FILE *fp = fopen("Makefile", "w");
	ASSERT(fp != nullptr, "could not create Makefile");
	fprintf_safe(fp, "all:\n\t@echo 'all target'\n");
	fprintf_safe(fp, "test:\n\ttouch test-ran\n");
	fclose(fp);

	options opt = {
		.inputs     = (char *[]){ "build", "test" },
		.inputs_num = 2,
		.jobs       = 2,
		.release    = true,
		.debug      = true,
		.features   = sdsnew("extra"),
	};
	i64 ret = handle_build(&opt);
	sdsfree(opt.features);
	ASSERT(ret == 0, "build with all legacy flags should succeed");
	ASSERT(access("test-ran", F_OK) == 0, "test target should have run");
	remove("test-ran");
	teardown_project("build-allflags");
	PASS();
}

TEST(build_target_validation)
{
	setup_project("build-tgtval", nullptr);

	/* Makefile defining the valid targets */
	FILE *fp = fopen("Makefile", "w");
	ASSERT(fp != nullptr, "could not create Makefile");
	fprintf_safe(fp, "all:\n\t@echo all\n");
	fprintf_safe(fp, "test:\n\t@echo test\n");
	fprintf_safe(fp, "hello.o:\n\t@echo hello.o\n");
	fprintf_safe(fp, "build/hello.o:\n\t@echo build/hello.o\n");
	fclose(fp);

	/* Invalid targets are rejected before make runs.  The charset is
	 * [A-Za-z0-9_.-], so '/' (a/b, build/hello.o) is rejected too. */
	char *invalid[] = { "", "-x", "a b", "a/b", "build/hello.o" };
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		options opt = {
			.inputs     = (char *[]){ "build", invalid[i] },
			.inputs_num = 2,
		};
		i64 ret = handle_build(&opt);
		ASSERT(ret == 1, "invalid target should be rejected");
	}

	/* Valid targets pass through to make */
	char *valid[] = { "all", "test", "hello.o" };
	for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
		options opt = {
			.inputs     = (char *[]){ "build", valid[i] },
			.inputs_num = 2,
		};
		i64 ret = handle_build(&opt);
		ASSERT(ret == 0, "valid target should succeed");
	}

	teardown_project("build-tgtval");
	PASS();
}

TEST(build_rejects_unimplemented_flags)
{
	setup_project("build-unimpl", nullptr);

	/* The parser accepts these, but no handler acts on them.  Silently
	 * building the default target and reporting success is worse than
	 * saying so. */
	struct {
		const char *flag;
		sds        *value;
	} cases[] = {
		{ "bin", nullptr },
		{ "example", nullptr },
		{ "profile", nullptr },
		{ "target", nullptr },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		sds     value = sdsnew("value");
		options opt   = {
			.inputs     = (char *[]){ "build" },
			.inputs_num = 1,
		};
		switch (i) {
		case 0:
			opt.bin = value;
			break;
		case 1:
			opt.example = value;
			break;
		case 2:
			opt.profile = value;
			break;
		default:
			opt.target = value;
			break;
		}
		i64 ret = handle_build(&opt);
		sdsfree(value);
		ASSERT(ret == 1, "an unimplemented build flag must be rejected");
	}

	teardown_project("build-unimpl");
	PASS();
}

TEST(run_with_unsafe_target_dir_falls_back)
{
	setup_project("run-unsafetdir", nullptr);

	/* Generated-mode fixture */
	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"run-unsafetdir\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);
	remove("Makefile");

	/* The Makefile rejects this target dir and builds into build/, so the
	 * resolved executable path must be build/<name> too — otherwise
	 * `coffee run` reports a binary that was never looked for in the
	 * right place. */
	options opt = {
		.inputs     = (char *[]){ "run" },
		.inputs_num = 1,
		.target_dir = sdsnew("build;rm -rf /"),
	};
	i64 ret = handle_run(&opt);
	sdsfree(opt.target_dir);
	ASSERT(ret == 0, "run with a rejected target_dir should fall back to build/");
	ASSERT(access("build/run-unsafetdir", X_OK) == 0, "binary should be built into build/");
	teardown_project("run-unsafetdir");
	PASS();
}

TEST(bench_generated_does_not_run_make)
{
	setup_project("bench-gen", nullptr);

	/* Generated-mode fixture with a legacy-shaped Makefile; running make
	 * would report success for work that never happened. */
	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"bench-gen\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);
	remove("Makefile");

	FILE *mk = fopen("Makefile", "w");
	ASSERT(mk != nullptr, "could not create Makefile");
	fprintf_safe(mk, "bench:\n\ttouch bench-ran\n");
	fclose(mk);

	options opt = {
		.inputs     = (char *[]){ "bench" },
		.inputs_num = 1,
	};
	i64 ret = handle_bench(&opt);
	ASSERT(ret == 0, "bench in generated mode should succeed");
	ASSERT(access("bench-ran", F_OK) != 0, "generated mode must not run the placeholder bench target");
	teardown_project("bench-gen");
	PASS();
}

TEST(build_generated_clean_target)
{
	setup_project("build-genclean", nullptr);

	/* Generated-mode fixture */
	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"build-genclean\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);
	remove("Makefile");

	options opt = {
		.inputs     = (char *[]){ "build" },
		.inputs_num = 1,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0, "build should succeed");
	ASSERT(access("build/build-genclean", X_OK) == 0, "binary should be built");

	opt.inputs     = (char *[]){ "build", "clean" };
	opt.inputs_num = 2;
	ret            = handle_build(&opt);
	ASSERT(ret == 0, "build clean should succeed");
	ASSERT(access("build", F_OK) != 0, "clean should remove build/");
	teardown_project("build-genclean");
	PASS();
}

TEST(build_generated_test_target)
{
	setup_project("build-gentest", nullptr);

	/* Generated-mode fixture */
	FILE *fp = fopen("Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"build-gentest\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);
	remove("Makefile");

	options opt = {
		.inputs     = (char *[]){ "build", "test" },
		.inputs_num = 2,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0, "build test should run the generated test target");
	teardown_project("build-gentest");
	PASS();
}

/* ---------------------------------------------------------------
 * tree
 * --------------------------------------------------------------- */
TEST(tree_no_manifest)
{
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	char old_cwd[4'096];
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
	TEST_REGISTER(build_generates_makefile);
	TEST_REGISTER(build_target_argument);
	TEST_REGISTER(build_legacy_requires_makefile);
	TEST_REGISTER(build_all_flags_legacy);
	TEST_REGISTER(build_target_validation);
	TEST_REGISTER(build_generated_clean_target);
	TEST_REGISTER(build_generated_test_target);
	TEST_REGISTER(build_rejects_unimplemented_flags);
	TEST_REGISTER(run_with_unsafe_target_dir_falls_back);
	TEST_REGISTER(bench_generated_does_not_run_make);
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
	TEST_REGISTER(run_from_subdirectory_generated);
	TEST_REGISTER(run_from_subdirectory_legacy);
	TEST_REGISTER(tree_no_manifest);
	TEST_REGISTER(tree_no_deps);
	TEST_REGISTER(generate_lockfile_no_manifest);
	TEST_REGISTER(generate_lockfile_no_deps);
	TEST_REGISTER(outdated_no_manifest);
	TEST_REGISTER(outdated_no_lockfile);
	TEST_REGISTER(update_no_manifest);
	TEST_REGISTER(update_no_deps);
}
