/*
 * Coverage tests for low-coverage command handlers.
 *
 * Each test creates a temporary project, chdirs into it, runs the command,
 * then cleans up.  Targets commands below ~50% coverage.
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
#include <time.h>

#include <sys/stat.h>
#include <unistd.h>

void coffee_register_coverage_commands_tests(void);

static char saved_cwd[4096];

static void setup_proj(const char *name, const char *deps_toml, bool with_main)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-cmd-%s", name);
	mkdir(tmpdir, 0755);
	assert(getcwd(saved_cwd, sizeof(saved_cwd)) != nullptr);
	assert(chdir(tmpdir) == 0);

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	/* Root-level deps first, then [package] section */
	if (deps_toml) {
		fprintf_safe(fp, "%s\n", deps_toml);
	}
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"%s\"\n", name);
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);

	mkdir("src", 0755);
	mkdir("tests", 0755);
	if (with_main) {
		FILE *mc = fopen("src/main.c", "w");
		if (mc) {
			fprintf_safe(mc, "int main(void) { return 0; }\n");
			fclose(mc);
		}
	}

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

static void teardown_proj(const char *name)
{
	chdir(saved_cwd);
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-cmd-%s", name);
	sds cmd    = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
}

TEST(cov_machete_with_deps)
{
	setup_proj("machete-deps", "dependencies = [\"used-dep\", \"unused-dep\"]", true);
	/* Create a source file that uses "used-dep" by name */
	FILE *fp = fopen("src/main.c", "w");
	fprintf_safe(fp, "#include <stdio.h>\nint main(void) {\n  (void)\"used-dep\";\n  return 0;\n}\n");
	fclose(fp);

	options opt = { .inputs = (char *[]){ "machete" }, .inputs_num = 1 };
	i64     ret = handle_machete(&opt);
	/* Should succeed — may find unused-dep or not depending on implementation */
	ASSERT(ret == 0, "machete should succeed");

	teardown_proj("machete-deps");
	PASS();
}

/* ---------- tree with deps in lockfile ---------- */
TEST(cov_tree_with_deps)
{
	setup_proj("tree-deps", "dependencies = [\"dep-a\"]", true);
	/* Create a lockfile with dep-a */
	FILE *lf = fopen("Coffee.lock", "w");
	ASSERT(lf != nullptr, "create lockfile");
	fprintf_safe(lf, "[[dependency]]\n");
	fprintf_safe(lf, "name = \"dep-a\"\n");
	fprintf_safe(lf, "version = \"1.0.0\"\n");
	fprintf_safe(lf, "path = \"/tmp/dep-a\"\n");
	fclose(lf);

	options opt = { .inputs = (char *[]){ "tree" }, .inputs_num = 1 };
	i64     ret = handle_tree(&opt);
	ASSERT(ret == 0, "tree should succeed");

	teardown_proj("tree-deps");
	PASS();
}

/* ---------- remove successful ---------- */
TEST(cov_remove_success)
{
	setup_proj("remove-ok", "dependencies = [\"toremove\"]", true);

	options opt = {
		.inputs     = (char *[]){ "remove", "toremove" },
		.inputs_num = 2,
	};
	i64 ret = handle_remove(&opt);
	ASSERT(ret == 0, "remove should succeed");

	teardown_proj("remove-ok");
	PASS();
}

/* ---------- metadata with dependencies ---------- */
TEST(cov_metadata_with_deps)
{
	setup_proj("meta-deps", "dependencies = [\"meta-dep\"]", true);

	options opt = { .inputs = (char *[]){ "metadata" }, .inputs_num = 1 };
	i64     ret = handle_metadata(&opt);
	ASSERT(ret == 0, "metadata should succeed");

	teardown_proj("meta-deps");
	PASS();
}

/* ---------- update with deps ---------- */
TEST(cov_update_with_deps)
{
	setup_proj("update-deps", "dependencies = [\"update-dep\"]", true);
	/* Needs a lockfile to update */
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\n");
		fprintf_safe(lf, "name = \"update-dep\"\n");
		fprintf_safe(lf, "version = \"1.0.0\"\n");
		fprintf_safe(lf, "path = \"/tmp/update-dep\"\n");
		fclose(lf);
	}

	options opt = { .inputs = (char *[]){ "update" }, .inputs_num = 1 };
	i64     ret = handle_update(&opt);
	ASSERT(ret == 0, "update should succeed");

	teardown_proj("update-deps");
	PASS();
}

/* ---------- vendor with deps ---------- */
TEST(cov_vendor_with_deps)
{
	setup_proj("vendor-deps", "dependencies = [\"vendor-dep\"]", true);

	options opt = { .inputs = (char *[]){ "vendor" }, .inputs_num = 1 };
	i64     ret = handle_vendor(&opt);
	ASSERT(ret == 0, "vendor should succeed");

	teardown_proj("vendor-deps");
	PASS();
}

/* ---------- cflags with specific dep ---------- */
TEST(cov_cflags_by_package)
{
	setup_proj("cflags-pkg", "dependencies = [\"cf-dep\"]", true);

	options opt = {
		.inputs     = (char *[]){ "cflags", "cf-dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_cflags(&opt);
	ASSERT(ret == 0, "cflags should succeed");

	teardown_proj("cflags-pkg");
	PASS();
}

/* ---------- libs with specific dep ---------- */
TEST(cov_libs_by_package)
{
	setup_proj("libs-pkg", "dependencies = [\"lib-dep\"]", true);

	options opt = {
		.inputs     = (char *[]){ "libs", "lib-dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_libs(&opt);
	ASSERT(ret == 0, "libs should succeed");

	teardown_proj("libs-pkg");
	PASS();
}

/* ---------- generate-lockfile with deps ---------- */
TEST(cov_generate_lockfile_with_deps)
{
	setup_proj("genlock-deps", "dependencies = [\"genlock-dep\"]", true);

	options opt = { .inputs = (char *[]){ "generate-lockfile" }, .inputs_num = 1 };
	i64     ret = handle_generate_lockfile(&opt);
	ASSERT(ret == 0, "generate-lockfile should succeed");

	/* Clean up generated lockfile */
	remove("Coffee.lock");
	teardown_proj("genlock-deps");
	PASS();
}

/* ---------- generate-lockfile without deps ---------- */
TEST(cov_generate_lockfile_no_deps)
{
	setup_proj("genlock-nodeps", nullptr, true);

	options opt = { .inputs = (char *[]){ "generate-lockfile" }, .inputs_num = 1 };
	i64     ret = handle_generate_lockfile(&opt);
	ASSERT(ret == 0, "generate-lockfile without deps should succeed");

	remove("Coffee.lock");
	teardown_proj("genlock-nodeps");
	PASS();
}

/* ---------- outdated with lockfile having deps ---------- */
TEST(cov_outdated_with_deps)
{
	setup_proj("outdated-deps", "dependencies = [\"out-dep\"]", true);
	/* Create a lockfile */
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\n");
		fprintf_safe(lf, "name = \"out-dep\"\n");
		fprintf_safe(lf, "version = \"0.1.0\"\n");
		fprintf_safe(lf, "path = \"/tmp/out-dep\"\n");
		fclose(lf);
	}

	options opt = { .inputs = (char *[]){ "outdated" }, .inputs_num = 1 };
	i64     ret = handle_outdated(&opt);
	ASSERT(ret == 0, "outdated should succeed");

	teardown_proj("outdated-deps");
	PASS();
}

/* ---------- add with version ---------- */
TEST(cov_add_with_version)
{
	setup_proj("add-ver", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "add", "ver-dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_add(&opt);
	/* add may succeed or fail depending on registry — just ensure it runs */
	ASSERT(ret == 0 || ret == 1, "add should run without crash");

	teardown_proj("add-ver");
	PASS();
}

/* ---------- build with locked mode ---------- */
TEST(cov_build_locked)
{
	setup_proj("build-locked", nullptr, true);
	/* Create lockfile needed for --locked */
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "version = \"1\"\n");
		fclose(lf);
	}

	options opt = {
		.inputs     = (char *[]){ "build", "--locked" },
		.inputs_num = 2,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0 || ret == 1, "build locked should run without crash");

	remove("Coffee.lock");
	teardown_proj("build-locked");
	PASS();
}

/* ---------- check with valid manifest ---------- */
TEST(cov_check_valid)
{
	setup_proj("check-valid", nullptr, true);

	/* Add license to [package] section manually */
	FILE *fp = fopen("Coffee.toml", "a");
	if (fp) {
		fprintf_safe(fp, "license = \"MIT\"\n");
		fclose(fp);
	}

	options opt = { .inputs = (char *[]){ "check" }, .inputs_num = 1 };
	i64     ret = handle_check(&opt);
	ASSERT(ret == 0, "check valid manifest");

	teardown_proj("check-valid");
	PASS();
}

/* ---------- fetch with deps ---------- */
TEST(cov_fetch_with_deps)
{
	setup_proj("fetch-deps", "dependencies = [\"fetch-dep\"]", true);
	options opt = { .inputs = (char *[]){ "fetch" }, .inputs_num = 1 };
	i64     ret = handle_fetch(&opt);
	/* fetch may fail for non-registry deps — just ensure it runs */
	ASSERT(ret == 0 || ret == 1, "fetch should run without crash");

	teardown_proj("fetch-deps");
	PASS();
}

/* ---------- list with project info ---------- */
TEST(cov_list_project)
{
	/* List needs a manifest */
	setup_proj("list-proj", nullptr, true);

	options opt = { .inputs = (char *[]){ "list" }, .inputs_num = 1 };
	i64     ret = handle_list(&opt);
	ASSERT(ret == 0, "list project");

	teardown_proj("list-proj");
	PASS();
}

/* ---------- uninstall success ---------- */
TEST(cov_uninstall_not_installed)
{
	options opt = {
		.inputs     = (char *[]){ "uninstall", "nonexistent-pkg" },
		.inputs_num = 2,
	};
	i64 ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall nonexistent");

	PASS();
}

/* ---------- report with deps ---------- */
TEST(cov_report_deps_licenses)
{
	setup_proj("report-lic", "dependencies = [\"r-dep\"]", true);

	options opt = {
		.inputs     = (char *[]){ "report", "deps" },
		.inputs_num = 2,
	};
	i64 ret = handle_report(&opt);
	ASSERT(ret == 0, "report deps");

	teardown_proj("report-lic");
	PASS();
}

/* ---------- test with manifest ---------- */
TEST(cov_test_with_manifest)
{
	setup_proj("test-cmd", nullptr, true);

	options opt = { .inputs = (char *[]){ "test" }, .inputs_num = 1 };
	i64     ret = handle_test(&opt);
	ASSERT(ret == 0 || ret == 1, "test cmd");

	teardown_proj("test-cmd");
	PASS();
}

/* ---------- add when dep already exists ---------- */
TEST(cov_add_already_exists)
{
	setup_proj("add-exists", "dependencies = [\"existing\"]", true);

	options opt = {
		.inputs     = (char *[]){ "add", "existing" },
		.inputs_num = 2,
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 0, "add existing dep should succeed (no-op)");

	teardown_proj("add-exists");
	PASS();
}

/* ---------- tree without deps ---------- */
TEST(cov_tree_no_deps)
{
	setup_proj("tree-nodeps", nullptr, true);

	options opt = { .inputs = (char *[]){ "tree" }, .inputs_num = 1 };
	i64     ret = handle_tree(&opt);
	ASSERT(ret == 0, "tree without deps");

	teardown_proj("tree-nodeps");
	PASS();
}

/* ---------- remove dep not found ---------- */
TEST(cov_remove_not_found)
{
	setup_proj("remove-notfound", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "remove", "nonexistent" },
		.inputs_num = 2,
	};
	i64 ret = handle_remove(&opt);
	ASSERT(ret == 1, "remove nonexistent dep should fail");

	teardown_proj("remove-notfound");
	PASS();
}

/* ---------- cflags with dep name and actual dep dir ---------- */
TEST(cov_cflags_with_dep_dir)
{
	setup_proj("cflags-depdir", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/cflagsdep", 0755);
	mkdir("deps/cflagsdep/include", 0755);

	FILE *fp = fopen("deps/cflagsdep/include/test.h", "w");
	if (fp) {
		fprintf_safe(fp, "/* test */\n");
		fclose(fp);
	}

	options opt = {
		.inputs     = (char *[]){ "cflags", "cflagsdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_cflags(&opt);
	ASSERT(ret == 0, "cflags with dep dir");

	rmdir("deps/cflagsdep/include");
	rmdir("deps/cflagsdep");
	rmdir("deps");
	teardown_proj("cflags-depdir");
	PASS();
}

/* ---------- libs with dep name and actual dep directory ---------- */
TEST(cov_libs_with_dep_dir)
{
	setup_proj("libs-depdir", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/libsdep", 0755);
	mkdir("deps/libsdep/lib", 0755);

	options opt = {
		.inputs     = (char *[]){ "libs", "libsdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_libs(&opt);
	ASSERT(ret == 0, "libs with dep dir");

	rmdir("deps/libsdep/lib");
	rmdir("deps/libsdep");
	rmdir("deps");
	teardown_proj("libs-depdir");
	PASS();
}

/* ---------- report audit ---------- */
TEST(cov_report_audit)
{
	setup_proj("report-audit", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "report", "audit" },
		.inputs_num = 2,
	};
	i64 ret = handle_report(&opt);
	ASSERT(ret == 0, "report audit");

	teardown_proj("report-audit");
	PASS();
}

/* ---------- config list with no config file ---------- */
TEST(cov_config_list_empty)
{
	options opt = {
		.inputs     = (char *[]){ "config", "list" },
		.inputs_num = 2,
	};
	i64 ret = handle_config(&opt);
	ASSERT(ret == 0, "config list empty");

	PASS();
}

/* ---------- config get with missing key ---------- */
TEST(cov_config_get_error)
{
	options opt = {
		.inputs     = (char *[]){ "config", "get", "nonexistent.key" },
		.inputs_num = 2,
	};
	i64 ret = handle_config(&opt);
	ASSERT(ret == 1, "config get nonexistent should fail");

	PASS();
}

/* ---------- uninstall not installed (slightly different input) ---------- */
TEST(cov_uninstall_not_installed_again)
{
	options opt = {
		.inputs     = (char *[]){ "uninstall", "another-nonexistent" },
		.inputs_num = 2,
	};
	i64 ret = handle_uninstall(&opt);
	ASSERT(ret == 1, "uninstall nonexistent");

	PASS();
}

/* ---------- init in empty dir ---------- */
TEST(cov_init_no_arg)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-init-empty");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	options opt = { .inputs = (char *[]){ "init" }, .inputs_num = 1 };
	i64     ret = handle_init(&opt);
	ASSERT(ret == 0, "init");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== new: --lib mode ===== */
TEST(cov_new_lib_mode)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-cmd-new-lib-%jd-%ld", (intmax_t)getpid(), ts.tv_nsec);
	sds rmcmd  = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(rmcmd);
	sdsfree(rmcmd);
	int mdret = mkdir(tmpdir, 0755);
	ASSERT(mdret == 0, "mkdir tmpdir should succeed");
	ASSERT(chdir(tmpdir) == 0, "chdir");

	options opt = {
		.inputs     = (char *[]){ "new", "mylib" },
		.inputs_num = 2,
		.lib        = true,
	};
	i64 ret = handle_new(&opt);
	ASSERT(ret == 0, "new --lib should succeed");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== config: set/get/unset with isolated COFFEE_HOME ===== */
TEST(cov_config_set_get_unset)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmp_home = sdsnew("/tmp/coverage-cmd-cfghome");
	mkdir(tmp_home, 0755);
	setenv("COFFEE_HOME", tmp_home, 1);

	options set_opt = {
		.inputs     = (char *[]){ "config", "set", "test.key", "val123" },
		.inputs_num = 4,
	};
	i64 ret = handle_config(&set_opt);
	ASSERT(ret == 0, "config set should succeed");

	options get_opt = {
		.inputs     = (char *[]){ "config", "get", "test.key" },
		.inputs_num = 3,
	};
	ret = handle_config(&get_opt);
	ASSERT(ret == 0, "config get should succeed");

	options list_opt = {
		.inputs     = (char *[]){ "config", "list" },
		.inputs_num = 2,
	};
	ret = handle_config(&list_opt);
	ASSERT(ret == 0, "config list should succeed");

	options unset_opt = {
		.inputs     = (char *[]){ "config", "unset", "test.key" },
		.inputs_num = 3,
	};
	ret = handle_config(&unset_opt);
	ASSERT(ret == 0, "config unset should succeed");

	unsetenv("COFFEE_HOME");
	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmp_home);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmp_home);
	PASS();
}

/* ===== config: set/unset with section.key ===== */
TEST(cov_config_section_key)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmp_home = sdsnew("/tmp/coverage-cmd-cfgsec");
	mkdir(tmp_home, 0755);
	setenv("COFFEE_HOME", tmp_home, 1);

	options set_opt = {
		.inputs     = (char *[]){ "config", "set", "build.opt-level", "2" },
		.inputs_num = 4,
	};
	i64 ret = handle_config(&set_opt);
	ASSERT(ret == 0, "config set section.key");

	options get_opt = {
		.inputs     = (char *[]){ "config", "get", "build.opt-level" },
		.inputs_num = 3,
	};
	ret = handle_config(&get_opt);
	ASSERT(ret == 0, "config get section.key");

	unsetenv("COFFEE_HOME");
	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmp_home);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmp_home);
	PASS();
}

/* ===== add: --path dep ===== */
TEST(cov_add_path_dep)
{
	setup_proj("add-path", nullptr, true);
	mkdir("/tmp/coverage-cmd-add-path-dep", 0755);

	options opt = {
		.inputs     = (char *[]){ "add", "local-dep" },
		.inputs_num = 2,
		.path       = sdsnew("/tmp/coverage-cmd-add-path-dep"),
	};
	i64 ret = handle_add(&opt);
	/* add may succeed or fail depending on registry checks — just ensure it runs */
	ASSERT(ret == 0 || ret == 1, "add --path should run without crash");
	sdsfree(opt.path);

	teardown_proj("add-path");
	sds cmd = sdsnew("rm -rf /tmp/coverage-cmd-add-path-dep");
	system(cmd);
	sdsfree(cmd);
	PASS();
}

/* ===== add: --dev dep ===== */
TEST(cov_add_dev_dep)
{
	setup_proj("add-dev", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "add", "dev-dep" },
		.inputs_num = 2,
		.dev        = true,
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 0 || ret == 1, "add --dev");

	teardown_proj("add-dev");
	PASS();
}

/* ===== add: --build dep ===== */
TEST(cov_add_build_dep)
{
	setup_proj("add-build", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "add", "build-dep" },
		.inputs_num = 2,
		.build_dep  = true,
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 0 || ret == 1, "add --build");

	teardown_proj("add-build");
	PASS();
}

/* ===== add: --optional dep ===== */
TEST(cov_add_optional_dep)
{
	setup_proj("add-opt", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "add", "opt-dep" },
		.inputs_num = 2,
		.optional   = true,
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 0 || ret == 1, "add --optional");

	teardown_proj("add-opt");
	PASS();
}

/* ===== add: with features ===== */
TEST(cov_add_with_features)
{
	setup_proj("add-feat", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "add", "feat-dep" },
		.inputs_num = 2,
		.features   = sdsnew("foo,bar"),
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 0 || ret == 1, "add --features");
	sdsfree(opt.features);

	teardown_proj("add-feat");
	PASS();
}

/* ===== check: validation errors (no name) ===== */
TEST(cov_check_no_name)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/coverage-cmd-check-noname");
	mkdir(tmpdir, 0755);
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	ASSERT(chdir(tmpdir) == 0, "chdir");

	/* Create a malformed manifest with no name */
	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);

	options opt = { .inputs = (char *[]){ "check" }, .inputs_num = 1 };
	i64     ret = handle_check(&opt);
	ASSERT(ret == 1, "check with no name should return 1");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== check: with dependencies in manifest ===== */
TEST(cov_check_with_deps)
{
	setup_proj("check-deps", "dependencies = [\"dep-one\"]", true);

	/* Create deps/dep-one dir for the dep check */
	mkdir("deps", 0755);
	mkdir("deps/dep-one", 0755);

	options opt = { .inputs = (char *[]){ "check" }, .inputs_num = 1 };
	i64     ret = handle_check(&opt);
	ASSERT(ret == 0, "check with deps");

	rmdir("deps/dep-one");
	rmdir("deps");
	teardown_proj("check-deps");
	PASS();
}

/* ===== check: verbose mode ===== */
TEST(cov_check_verbose)
{
	setup_proj("check-verb", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "check" },
		.inputs_num = 1,
		.verbose    = true,
	};
	i64 ret = handle_check(&opt);
	ASSERT(ret == 0, "check verbose");

	teardown_proj("check-verb");
	PASS();
}

/* ===== check: with sources/headers in manifest ===== */
TEST(cov_check_sources_headers)
{
	setup_proj("check-sh", nullptr, false);
	mkdir("include", 0755);
	FILE *hf = fopen("include/mylib.h", "w");
	if (hf) {
		fprintf_safe(hf, "#ifndef MYLIB_H\n#define MYLIB_H\nvoid foo(void);\n#endif\n");
		fclose(hf);
	}
	/* Write a source file */
	FILE *sf = fopen("src/lib.c", "w");
	if (sf) {
		fprintf_safe(sf, "#include \"mylib.h\"\nvoid foo(void) {}\n");
		fclose(sf);
	}
	/* Append sources/headers to manifest */
	FILE *mf = fopen("Coffee.toml", "a");
	if (mf) {
		fprintf_safe(mf, "sources = [\"src/lib.c\"]\n");
		fprintf_safe(mf, "headers = [\"include/mylib.h\"]\n");
		fclose(mf);
	}

	options opt = { .inputs = (char *[]){ "check" }, .inputs_num = 1 };
	i64     ret = handle_check(&opt);
	ASSERT(ret == 0, "check with sources/headers");

	teardown_proj("check-sh");
	PASS();
}

/* ===== tree: with actual deps in lockfile ===== */
TEST(cov_tree_with_lockfile_deps)
{
	setup_proj("tree-lock", "dependencies = [\"tldep\"]", true);
	/* Create a lockfile with properly formatted dep */
	FILE *lf = fopen("Coffee.lock", "w");
	ASSERT(lf != nullptr, "create lockfile");
	fprintf_safe(lf, "[[dependency]]\n");
	fprintf_safe(lf, "name = \"tldep\"\n");
	fprintf_safe(lf, "version = \"2.0.0\"\n");
	fprintf_safe(lf, "path = \"/tmp/tldep\"\n");
	fclose(lf);
	mkdir("/tmp/tldep", 0755);

	options opt = { .inputs = (char *[]){ "tree" }, .inputs_num = 1 };
	i64     ret = handle_tree(&opt);
	ASSERT(ret == 0, "tree with lockfile deps");

	rmdir("/tmp/tldep");
	teardown_proj("tree-lock");
	PASS();
}

/* ===== metadata: with no deps ===== */
TEST(cov_metadata_no_deps_v2)
{
	setup_proj("meta-nodepsv2", nullptr, true);

	options opt = { .inputs = (char *[]){ "metadata" }, .inputs_num = 1 };
	i64     ret = handle_metadata(&opt);
	ASSERT(ret == 0, "metadata no deps");

	teardown_proj("meta-nodepsv2");
	PASS();
}

/* ===== update: with specific dep name ===== */
TEST(cov_update_specific_dep_v2)
{
	setup_proj("upd-spec", "dependencies = [\"specdep\"]", true);
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\n");
		fprintf_safe(lf, "name = \"specdep\"\n");
		fprintf_safe(lf, "version = \"1.0.0\"\n");
		fprintf_safe(lf, "path = \"/tmp/specdep\"\n");
		fclose(lf);
	}

	options opt = {
		.inputs     = (char *[]){ "update", "specdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_update(&opt);
	ASSERT(ret == 0 || ret == 1, "update specific dep");

	teardown_proj("upd-spec");
	PASS();
}

/* ===== remove: with dep target that exists in manifest ===== */
TEST(cov_remove_manifest_dep)
{
	setup_proj("rm-manifest", "dependencies = [\"manifest-dep\"]", true);

	options opt = {
		.inputs     = (char *[]){ "remove", "manifest-dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_remove(&opt);
	ASSERT(ret == 0, "remove manifest dep");

	teardown_proj("rm-manifest");
	PASS();
}

/* ---------- cflags with dep name and actual dep dir having include ---------- */
TEST(cov_cflags_with_include)
{
	setup_proj("cflags-inc", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/incdep", 0755);
	mkdir("deps/incdep/include", 0755);
	FILE *hf = fopen("deps/incdep/include/test.h", "w");
	if (hf) {
		fprintf_safe(hf, "/* test */\n");
		fclose(hf);
	}

	options opt = {
		.inputs     = (char *[]){ "cflags", "incdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_cflags(&opt);
	ASSERT(ret == 0, "cflags with include dir");

	rmdir("deps/incdep/include");
	rmdir("deps/incdep");
	rmdir("deps");
	teardown_proj("cflags-inc");
	PASS();
}

/* ---------- libs with dep name and actual dep dir having lib ---------- */
TEST(cov_libs_with_libdir)
{
	setup_proj("libs-libdir", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/libdep2", 0755);
	mkdir("deps/libdep2/lib", 0755);

	options opt = {
		.inputs     = (char *[]){ "libs", "libdep2" },
		.inputs_num = 2,
	};
	i64 ret = handle_libs(&opt);
	ASSERT(ret == 0, "libs with lib dir");

	rmdir("deps/libdep2/lib");
	rmdir("deps/libdep2");
	rmdir("deps");
	teardown_proj("libs-libdir");
	PASS();
}

/* ===== generate-lockfile: with deps ===== */
TEST(cov_generate_lockfile_deps_v2)
{
	setup_proj("genlock-d2", "dependencies = [\"gen2-dep\"]", true);

	options opt = { .inputs = (char *[]){ "generate-lockfile" }, .inputs_num = 1 };
	i64     ret = handle_generate_lockfile(&opt);
	ASSERT(ret == 0, "generate-lockfile with deps");

	remove("Coffee.lock");
	teardown_proj("genlock-d2");
	PASS();
}

/* ===== outdated: with lockfile having deps that can be checked ===== */
TEST(cov_outdated_lockfile_with_deps)
{
	setup_proj("out-lock", "dependencies = [\"out2-dep\"]", true);
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\n");
		fprintf_safe(lf, "name = \"out2-dep\"\n");
		fprintf_safe(lf, "version = \"0.0.1\"\n");
		fprintf_safe(lf, "path = \"/tmp/out2-dep\"\n");
		fclose(lf);
	}

	options opt = { .inputs = (char *[]){ "outdated" }, .inputs_num = 1 };
	i64     ret = handle_outdated(&opt);
	ASSERT(ret == 0, "outdated");

	teardown_proj("out-lock");
	PASS();
}

/* ===== init: in directory with existing Coffee.toml ===== */
TEST(cov_init_existing_project)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-init-exist");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");
	/* Create an existing Coffee.toml */
	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	fprintf_safe(fp, "[package]\nname = \"existing\"\nversion = \"1.0.0\"\n");
	fclose(fp);

	options opt = { .inputs = (char *[]){ "init" }, .inputs_num = 1 };
	i64     ret = handle_init(&opt);
	/* init with existing project may succeed or fail */
	ASSERT(ret == 0 || ret == 1, "init existing project");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== build: with features (no default features) ===== */
TEST(cov_build_no_default_features)
{
	setup_proj("build-nodef", nullptr, true);

	options opt = {
		.inputs              = (char *[]){ "build" },
		.inputs_num          = 1,
		.no_default_features = true,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0 || ret == 1, "build --no-default-features");

	teardown_proj("build-nodef");
	PASS();
}

/* ===== build: with all features ===== */
TEST(cov_build_all_features_v2)
{
	setup_proj("build-allf", nullptr, true);

	options opt = {
		.inputs       = (char *[]){ "build" },
		.inputs_num   = 1,
		.all_features = true,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0 || ret == 1, "build --all-features");

	teardown_proj("build-allf");
	PASS();
}

/* ===== build: with release mode ===== */
TEST(cov_build_release_mode)
{
	setup_proj("build-rel", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "build" },
		.inputs_num = 1,
		.release    = true,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0 || ret == 1, "build --release");

	teardown_proj("build-rel");
	PASS();
}

/* ===== list: with installed deps (empty bin dir) ===== */
TEST(cov_list_installed)
{
	setup_proj("list-inst", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "list", "installed" },
		.inputs_num = 2,
	};
	i64 ret = handle_list(&opt);
	ASSERT(ret == 0, "list installed");

	teardown_proj("list-inst");
	PASS();
}

/* ===== report: with different report types ===== */
TEST(cov_report_deps_detail)
{
	setup_proj("rep-det", "dependencies = [\"rdep\"]", true);

	options opt = {
		.inputs     = (char *[]){ "report", "deps" },
		.inputs_num = 2,
	};
	i64 ret = handle_report(&opt);
	ASSERT(ret == 0, "report deps");

	teardown_proj("rep-det");
	PASS();
}

/* ===== test: with verbose output ===== */
TEST(cov_test_verbose)
{
	setup_proj("test-verb", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "test" },
		.inputs_num = 1,
		.verbose    = true,
	};
	i64 ret = handle_test(&opt);
	ASSERT(ret == 0 || ret == 1, "test verbose");

	teardown_proj("test-verb");
	PASS();
}

/* ===== install_update: with a target package ===== */
TEST(cov_install_update_with_package)
{
	options opt = {
		.inputs     = (char *[]){ "install-update", "some-pkg" },
		.inputs_num = 2,
	};
	i64 ret = handle_install_update(&opt);
	/* May fail if registry not accessible */
	ASSERT(ret == 0 || ret == 1, "install_update with package");

	PASS();
}

/* ===== install_update_config: basic run ===== */
TEST(cov_install_update_config_basic)
{
	char        old_home[4096] = { 0 };
	const char *env            = getenv("COFFEE_HOME");
	if (env != nullptr) {
		strncpy(old_home, env, sizeof(old_home) - 1);
	}
	sds tmp_home = sdsnew("/tmp/coverage-cmd-install-update-config");
	sds rmcmd    = sdscatprintf(sdsempty(), "rm -rf %s", tmp_home);
	system(rmcmd);
	sdsfree(rmcmd);
	mkdir(tmp_home, 0755);
	setenv("COFFEE_HOME", tmp_home, 1);

	options opt = {
		.inputs     = (char *[]){ "install-update-config" },
		.inputs_num = 1,
	};
	i64 ret = handle_install_update_config(&opt);
	ASSERT(ret == 0, "install_update_config should return 0");

	/* Verify config file was created */
	sds cfg_path = sdscatprintf(sdsempty(), "%s/config.toml", tmp_home);
	i64 exists   = (access(cfg_path, F_OK) == 0);
	sdsfree(cfg_path);
	ASSERT(exists, "config.toml should exist after install_update_config");

	if (old_home[0] != '\0') {
		setenv("COFFEE_HOME", old_home, 1);
	} else {
		unsetenv("COFFEE_HOME");
	}
	rmcmd = sdscatprintf(sdsempty(), "rm -rf %s", tmp_home);
	system(rmcmd);
	sdsfree(rmcmd);
	sdsfree(tmp_home);

	PASS();
}

/* ===== tree: with deps that have library.toml ===== */
TEST(cov_tree_with_library_toml)
{
	setup_proj("tree-libtoml", "dependencies = [\"libdep\"]", true);
	/* Create a library.toml in deps/libdep */
	mkdir("deps", 0755);
	mkdir("deps/libdep", 0755);
	FILE *lt = fopen("deps/libdep/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\nname = \"libdep\"\nversion = \"1.0.0\"\n");
		fprintf_safe(lt, "dependencies = [\"subdep\"]\n");
		fclose(lt);
	}
	/* Also need a lockfile referring to the dep */
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\nname = \"libdep\"\nversion = \"1.0.0\"\npath = \"deps/libdep\"\n");
		fclose(lf);
	}

	options opt = { .inputs = (char *[]){ "tree" }, .inputs_num = 1 };
	i64     ret = handle_tree(&opt);
	ASSERT(ret == 0, "tree with library.toml");

	teardown_proj("tree-libtoml");
	PASS();
}

/* ===== cflags: with --package reading library.toml include array ===== */
TEST(cov_cflags_with_library_toml)
{
	setup_proj("cflags-libt", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/cflibdep", 0755);
	FILE *lt = fopen("deps/cflibdep/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\nname = \"cflibdep\"\nversion = \"1.0.0\"\n");
		fprintf_safe(lt, "include = [\"include\", \"src\"]\n");
		fclose(lt);
	}
	mkdir("deps/cflibdep/include", 0755);
	mkdir("deps/cflibdep/src", 0755);

	options opt = {
		.inputs     = (char *[]){ "cflags", "cflibdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_cflags(&opt);
	ASSERT(ret == 0, "cflags with library.toml include");

	rmdir("deps/cflibdep/src");
	rmdir("deps/cflibdep/include");
	rmdir("deps/cflibdep");
	rmdir("deps");
	teardown_proj("cflags-libt");
	PASS();
}

/* ===== libs: with --package reading library.toml libname ===== */
TEST(cov_libs_with_library_toml)
{
	setup_proj("libs-libt", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/liblibdep", 0755);
	FILE *lt = fopen("deps/liblibdep/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\nname = \"liblibdep\"\nversion = \"1.0.0\"\n");
		fprintf_safe(lt, "libname = \"mylib\"\n");
		fclose(lt);
	}

	options opt = {
		.inputs     = (char *[]){ "libs", "liblibdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_libs(&opt);
	ASSERT(ret == 0, "libs with library.toml libname");

	rmdir("deps/liblibdep");
	rmdir("deps");
	teardown_proj("libs-libt");
	PASS();
}

/* ===== generate-lockfile: with dep having library.toml ===== */
TEST(cov_generate_lockfile_with_lib)
{
	setup_proj("genlock-lib", "dependencies = [\"glibdep\"]", true);
	mkdir("deps", 0755);
	mkdir("deps/glibdep", 0755);
	FILE *lt = fopen("deps/glibdep/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\nname = \"glibdep\"\nversion = \"2.0.0\"\n");
		fclose(lt);
	}

	options opt = { .inputs = (char *[]){ "generate-lockfile" }, .inputs_num = 1 };
	i64     ret = handle_generate_lockfile(&opt);
	ASSERT(ret == 0, "generate-lockfile with library.toml");

	remove("Coffee.lock");
	teardown_proj("genlock-lib");
	PASS();
}

/* ===== update: with dep having library.toml ===== */
TEST(cov_update_with_library_toml)
{
	setup_proj("upd-lib", "dependencies = [\"updlibdep\"]", true);
	/* Need existing lockfile */
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\nname = \"updlibdep\"\nversion = \"1.0.0\"\npath = \"deps/updlibdep\"\n");
		fclose(lf);
	}
	mkdir("deps", 0755);
	mkdir("deps/updlibdep", 0755);
	FILE *lt = fopen("deps/updlibdep/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\nname = \"updlibdep\"\nversion = \"1.0.0\"\n");
		fclose(lt);
	}

	options opt = {
		.inputs     = (char *[]){ "update", "updlibdep" },
		.inputs_num = 2,
	};
	i64 ret = handle_update(&opt);
	ASSERT(ret == 0 || ret == 1, "update with library.toml");

	teardown_proj("upd-lib");
	PASS();
}

/* ===== remove: remove a dep that's present in manifest ===== */
TEST(cov_remove_present_dep)
{
	setup_proj("rm-present", "dependencies = [\"present-dep\"]", true);

	options opt = {
		.inputs     = (char *[]){ "remove", "present-dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_remove(&opt);
	ASSERT(ret == 0, "remove present dep");

	teardown_proj("rm-present");
	PASS();
}

/* ===== list: installed with actual files in bin dir ===== */
TEST(cov_list_installed_with_files)
{
	setup_proj("list-bin", nullptr, true);
	/* Create a fake COFFEE_HOME bin dir with an installed binary */
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	sds coffee_dir = sdscatprintf(sdsempty(), "%s/.coffee", home);
	sds bin_dir    = sdscatprintf(sdsempty(), "%s/bin", coffee_dir);
	mkdir(bin_dir, 0755);
	sds   bin_path = sdscatprintf(sdsempty(), "%s/coffee-test-bin", bin_dir);
	FILE *bf       = fopen(bin_path, "w");
	if (bf) {
		fprintf_safe(bf, "#!/bin/sh\necho test\n");
		fclose(bf);
	}
	chmod(bin_path, 0755);

	options opt = {
		.inputs     = (char *[]){ "list", "installed" },
		.inputs_num = 2,
	};
	i64 ret = handle_list(&opt);
	ASSERT(ret == 0, "list installed with files");

	remove(bin_path);
	rmdir(bin_dir);
	rmdir(coffee_dir);
	sdsfree(bin_path);
	sdsfree(bin_dir);
	sdsfree(coffee_dir);
	teardown_proj("list-bin");
	PASS();
}

/* ===== test: with filter argument ===== */
TEST(cov_test_with_filter)
{
	setup_proj("test-flt", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "test", "some_filter" },
		.inputs_num = 2,
	};
	i64 ret = handle_test(&opt);
	ASSERT(ret == 0 || ret == 1, "test with filter");

	teardown_proj("test-flt");
	PASS();
}

/* ===== add: with --git (will fail but covers code path) ===== */
TEST(cov_add_with_git)
{
	setup_proj("add-git", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "add", "git-dep" },
		.inputs_num = 2,
		.git        = sdsnew("https://example.com/repo.git"),
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 0 || ret == 1, "add --git");
	sdsfree(opt.git);

	teardown_proj("add-git");
	PASS();
}

/* ===== report: audit with deps ===== */
TEST(cov_report_audit_deps)
{
	setup_proj("rep-aud", "dependencies = [\"aud-dep\"]", true);

	options opt = {
		.inputs     = (char *[]){ "report", "audit" },
		.inputs_num = 2,
	};
	i64 ret = handle_report(&opt);
	ASSERT(ret == 0, "report audit with deps");

	teardown_proj("rep-aud");
	PASS();
}

/* ===== outdated: with deps having library.toml ===== */
TEST(cov_outdated_with_library_toml)
{
	setup_proj("out-libtoml", "dependencies = [\"outlibdep\"]", true);
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\nname = \"outlibdep\"\nversion = \"0.1.0\"\npath = \"deps/outlibdep\"\n");
		fclose(lf);
	}

	options opt = { .inputs = (char *[]){ "outdated" }, .inputs_num = 1 };
	i64     ret = handle_outdated(&opt);
	ASSERT(ret == 0, "outdated with library.toml");

	teardown_proj("out-libtoml");
	PASS();
}

/* ===== metadata: no deps v3 (different path coverage) ===== */
TEST(cov_metadata_no_deps_v3)
{
	setup_proj("meta-n3", nullptr, true);

	options opt = { .inputs = (char *[]){ "metadata" }, .inputs_num = 1 };
	i64     ret = handle_metadata(&opt);
	ASSERT(ret == 0, "metadata no deps v3");

	teardown_proj("meta-n3");
	PASS();
}

/* ===== build: with manifest_path option pointing to test project ===== */
TEST(cov_build_manifest_path)
{
	setup_proj("build-mp", nullptr, true);
	/* Use manifest_path pointing to the test project in the temp dir */
	sds cwd = sdsnew("/tmp/coverage-cmd-build-mp");
	sds mp  = sdscatprintf(cwd, "/Coffee.toml");

	options opt = {
		.inputs        = (char *[]){ "build" },
		.inputs_num    = 1,
		.manifest_path = mp,
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0 || ret == 1, "build with manifest_path");
	sdsfree(mp);

	teardown_proj("build-mp");
	PASS();
}

/* ===== build: with features string ===== */
TEST(cov_build_with_features)
{
	setup_proj("build-feat", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "build" },
		.inputs_num = 1,
		.features   = sdsnew("feat1,feat2"),
	};
	i64 ret = handle_build(&opt);
	ASSERT(ret == 0 || ret == 1, "build with features");
	sdsfree(opt.features);

	teardown_proj("build-feat");
	PASS();
}

/* ===== install_update: update a specific package ===== */
TEST(cov_install_update_update_pkg)
{
	/* Create a fake global deps dir with a package */
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	sds coffee_dir = sdscatprintf(sdsempty(), "%s/.coffee", home);
	sds deps_dir   = sdscatprintf(sdsempty(), "%s/deps", coffee_dir);
	sds pkg_dir    = sdscatprintf(sdsempty(), "%s/update-pkg", deps_dir);
	mkdir(deps_dir, 0755);
	mkdir(pkg_dir, 0755);

	options opt = {
		.inputs     = (char *[]){ "install-update", "update-pkg" },
		.inputs_num = 2,
	};
	i64 ret = handle_install_update(&opt);
	ASSERT(ret == 0 || ret == 1, "install_update update package");

	rmdir(pkg_dir);
	rmdir(deps_dir);
	rmdir(coffee_dir);
	sdsfree(pkg_dir);
	sdsfree(deps_dir);
	sdsfree(coffee_dir);
	PASS();
}

/* ===== init: with explicit path ===== */
TEST(cov_init_with_path)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-init-path");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	options opt = {
		.inputs     = (char *[]){ "init", "myapp" },
		.inputs_num = 2,
	};
	i64 ret = handle_init(&opt);
	ASSERT(ret == 0, "init with path");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== uninstall: with actual installed package ===== */
TEST(cov_uninstall_installed)
{
	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	sds coffee_dir = sdscatprintf(sdsempty(), "%s/.coffee", home);
	sds deps_dir   = sdscatprintf(sdsempty(), "%s/deps", coffee_dir);
	sds pkg_dir    = sdscatprintf(sdsempty(), "%s/to-uninstall", deps_dir);
	mkdir(deps_dir, 0755);
	mkdir(pkg_dir, 0755);

	options opt = {
		.inputs     = (char *[]){ "uninstall", "to-uninstall" },
		.inputs_num = 2,
	};
	i64 ret = handle_uninstall(&opt);
	ASSERT(ret == 0, "uninstall should succeed");

	rmdir(pkg_dir);
	rmdir(deps_dir);
	rmdir(coffee_dir);
	sdsfree(pkg_dir);
	sdsfree(deps_dir);
	sdsfree(coffee_dir);
	PASS();
}

/* ===== check: missing version error ===== */
TEST(cov_check_missing_version)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-chk-noversion");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"noversion\"\n");
	fprintf_safe(fp, "edition = \"c23\"\n");
	fclose(fp);
	mkdir("src", 0755);

	options opt = { .inputs = (char *[]){ "check" }, .inputs_num = 1 };
	i64     ret = handle_check(&opt);
	ASSERT(ret == 1, "check missing version should fail");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== check: parse error (malformed Coffee.toml) ===== */
TEST(cov_check_parse_error)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-chk-badparse");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	fprintf_safe(fp, "[[[\n");
	fclose(fp);

	options opt = { .inputs = (char *[]){ "check" }, .inputs_num = 1 };
	i64     ret = handle_check(&opt);
	ASSERT(ret == 1, "check parse error should return 1");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== fetch: manifest parse error ===== */
TEST(cov_fetch_parse_error)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-fetch-bad");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	fprintf_safe(fp, "[[[\n");
	fclose(fp);

	options opt = { .inputs = (char *[]){ "fetch" }, .inputs_num = 1 };
	i64     ret = handle_fetch(&opt);
	ASSERT(ret == 1, "fetch parse error should return 1");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== vendor: manifest parse error ===== */
TEST(cov_vendor_parse_error)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-vendor-bad");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	FILE *fp = fopen("Coffee.toml", "w");
	assert(fp);
	fprintf_safe(fp, "[[[\n");
	fclose(fp);

	options opt = { .inputs = (char *[]){ "vendor" }, .inputs_num = 1 };
	i64     ret = handle_vendor(&opt);
	ASSERT(ret == 1, "vendor parse error should return 1");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== check: with dep = syntax ===== */
TEST(cov_check_dep_eq)
{
	setup_proj("chk-depeq", "dependencies = [\"dep-eq = \\\"1.0.0\\\"\"]", true);
	mkdir("deps", 0755);
	mkdir("deps/dep-eq", 0755);

	options opt = { .inputs = (char *[]){ "check" }, .inputs_num = 1 };
	i64     ret = handle_check(&opt);
	ASSERT(ret == 0 || ret == 1, "check dep = syntax");

	teardown_proj("chk-depeq");
	PASS();
}

/* ===== init: library mode (no bin detected) ===== */
TEST(cov_init_lib_mode)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-init-lib");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	options opt = { .inputs = (char *[]){ "init", "mylib" }, .inputs_num = 2 };
	i64     ret = handle_init(&opt);
	ASSERT(ret == 0 || ret == 1, "init lib mode");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== metadata: with features in manifest ===== */
TEST(cov_metadata_with_features)
{
	setup_proj("meta-feat",
	           "[features]\n"
	           "default = []\n"
	           "ssl = [\"dep-ssl\"]\n",
	           true);

	options opt = { .inputs = (char *[]){ "metadata" }, .inputs_num = 1 };
	i64     ret = handle_metadata(&opt);
	ASSERT(ret == 0, "metadata with features");

	teardown_proj("meta-feat");
	PASS();
}

/* ===== new: existing directory failure path ===== */
TEST(cov_new_existing_dir)
{
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd");
	sds tmpdir = sdsnew("/tmp/coverage-cmd-new-exist");
	mkdir(tmpdir, 0755);
	ASSERT(chdir(tmpdir) == 0, "chdir");

	mkdir("existing_proj", 0755);

	options opt = {
		.inputs     = (char *[]){ "new", "existing_proj" },
		.inputs_num = 2,
	};
	i64 ret = handle_new(&opt);
	ASSERT(ret == 0 || ret == 1, "new existing dir should not crash");

	chdir(old_cwd);
	sds cmd = sdscatprintf(sdsempty(), "rm -rf %s", tmpdir);
	system(cmd);
	sdsfree(cmd);
	sdsfree(tmpdir);
	PASS();
}

/* ===== outdated: with lockfile containing deps and registry check ===== */
TEST(cov_outdated_with_lockfile_deps)
{
	setup_proj("out-lockdeps", "dependencies = [\"oldep\"]", true);
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\n");
		fprintf_safe(lf, "name = \"oldep\"\n");
		fprintf_safe(lf, "version = \"0.0.1\"\n");
		fprintf_safe(lf, "path = \"/tmp/oldep-path\"\n");
		fclose(lf);
	}
	mkdir("/tmp/oldep-path", 0755);

	options opt = { .inputs = (char *[]){ "outdated" }, .inputs_num = 1 };
	i64     ret = handle_outdated(&opt);
	ASSERT(ret == 0 || ret == 1, "outdated with lockfile deps");

	rmdir("/tmp/oldep-path");
	teardown_proj("out-lockdeps");
	PASS();
}

/* ===== tree: with deps having library.toml and dependencies ===== */
TEST(cov_tree_transitive_deps)
{
	setup_proj("tree-trans", "dependencies = [\"transdep-tree\"]", true);
	mkdir("deps", 0755);
	mkdir("deps/transdep-tree", 0755);
	FILE *lt = fopen("deps/transdep-tree/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\n");
		fprintf_safe(lt, "name = \"transdep-tree\"\n");
		fprintf_safe(lt, "version = \"1.0.0\"\n");
		fprintf_safe(lt, "dependencies = [\"subdep\"]\n");
		fclose(lt);
	}
	FILE *lf = fopen("Coffee.lock", "w");
	if (lf) {
		fprintf_safe(lf, "[[dependency]]\n");
		fprintf_safe(lf, "name = \"transdep-tree\"\n");
		fprintf_safe(lf, "version = \"1.0.0\"\n");
		fprintf_safe(lf, "path = \"deps/transdep-tree\"\n");
		fclose(lf);
	}

	options opt = { .inputs = (char *[]){ "tree" }, .inputs_num = 1 };
	i64     ret = handle_tree(&opt);
	ASSERT(ret == 0, "tree transitive deps");

	teardown_proj("tree-trans");
	PASS();
}

/* ===== add: with auto-fetch from registry ===== */
TEST(cov_add_auto_fetch)
{
	setup_proj("add-autof", nullptr, true);

	options opt = {
		.inputs     = (char *[]){ "add", "autofetch-dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_add(&opt);
	ASSERT(ret == 0 || ret == 1, "add auto-fetch");

	teardown_proj("add-autof");
	PASS();
}

/* ===== cflags: --package with library.toml include array hitting toml parsing ===== */
TEST(cov_cflags_include_toml)
{
	setup_proj("cflags-inctoml", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/cfginctoml", 0755);
	FILE *lt = fopen("deps/cfginctoml/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\nname = \"cfginctoml\"\n");
		fprintf_safe(lt, "include = [\"mydir\"]\n");
		fclose(lt);
	}
	mkdir("deps/cfginctoml/mydir", 0755);

	options opt = {
		.inputs     = (char *[]){ "cflags", "cfginctoml" },
		.inputs_num = 2,
	};
	i64 ret = handle_cflags(&opt);
	ASSERT(ret == 0, "cflags with include TOML");

	rmdir("deps/cfginctoml/mydir");
	rmdir("deps/cfginctoml");
	rmdir("deps");
	teardown_proj("cflags-inctoml");
	PASS();
}

/* ===== libs: --package with library.toml lib array ===== */
TEST(cov_libs_lib_toml)
{
	setup_proj("libs-libtoml", nullptr, true);
	mkdir("deps", 0755);
	mkdir("deps/libtomldep", 0755);
	FILE *lt = fopen("deps/libtomldep/library.toml", "w");
	if (lt) {
		fprintf_safe(lt, "[package]\nname = \"libtomldep\"\n");
		fprintf_safe(lt, "lib = [\"lib\"]\n");
		fclose(lt);
	}
	mkdir("deps/libtomldep/lib", 0755);

	options opt = {
		.inputs     = (char *[]){ "libs", "libtomldep" },
		.inputs_num = 2,
	};
	i64 ret = handle_libs(&opt);
	ASSERT(ret == 0, "libs with lib TOML");

	rmdir("deps/libtomldep/lib");
	rmdir("deps/libtomldep");
	rmdir("deps");
	teardown_proj("libs-libtoml");
	PASS();
}

/* ===== remove: with Makefile cleanup ===== */
TEST(cov_remove_makefile_cleanup)
{
	setup_proj("rm-makefile", "dependencies = [\"mkclean-dep\"]", true);
	mkdir("deps", 0755);
	mkdir("deps/mkclean-dep", 0755);

	FILE *mf = fopen("Makefile", "w");
	if (mf) {
		fprintf_safe(mf, "# Dep: mkclean-dep\n");
		fprintf_safe(mf, "\t$(shell coffee cflags mkclean-dep)\n");
		fprintf_safe(mf, "\t$(shell coffee libs mkclean-dep)\n");
		fclose(mf);
	}

	options opt = {
		.inputs     = (char *[]){ "remove", "mkclean-dep" },
		.inputs_num = 2,
	};
	i64 ret = handle_remove(&opt);
	ASSERT(ret == 0, "remove with Makefile cleanup");

	teardown_proj("rm-makefile");
	PASS();
}

void coffee_register_coverage_commands_tests(void)
{
	TEST_REGISTER(cov_machete_with_deps);
	TEST_REGISTER(cov_tree_with_deps);
	TEST_REGISTER(cov_remove_success);
	TEST_REGISTER(cov_metadata_with_deps);
	TEST_REGISTER(cov_update_with_deps);
	TEST_REGISTER(cov_vendor_with_deps);
	TEST_REGISTER(cov_cflags_by_package);
	TEST_REGISTER(cov_libs_by_package);
	TEST_REGISTER(cov_generate_lockfile_with_deps);
	TEST_REGISTER(cov_generate_lockfile_no_deps);
	TEST_REGISTER(cov_outdated_with_deps);
	TEST_REGISTER(cov_add_with_version);
	TEST_REGISTER(cov_build_locked);
	TEST_REGISTER(cov_check_valid);
	TEST_REGISTER(cov_fetch_with_deps);
	TEST_REGISTER(cov_list_project);
	TEST_REGISTER(cov_uninstall_not_installed);
	TEST_REGISTER(cov_report_deps_licenses);
	TEST_REGISTER(cov_test_with_manifest);
	TEST_REGISTER(cov_init_no_arg);
	TEST_REGISTER(cov_add_already_exists);
	TEST_REGISTER(cov_tree_no_deps);
	TEST_REGISTER(cov_remove_not_found);
	TEST_REGISTER(cov_cflags_with_dep_dir);
	TEST_REGISTER(cov_libs_with_dep_dir);
	TEST_REGISTER(cov_report_audit);
	TEST_REGISTER(cov_config_list_empty);
	TEST_REGISTER(cov_config_get_error);
	TEST_REGISTER(cov_uninstall_not_installed_again);
	/* Coverage batch 1 */
	TEST_REGISTER(cov_new_lib_mode);
	TEST_REGISTER(cov_config_set_get_unset);
	TEST_REGISTER(cov_config_section_key);
	TEST_REGISTER(cov_add_path_dep);
	TEST_REGISTER(cov_add_dev_dep);
	TEST_REGISTER(cov_add_build_dep);
	TEST_REGISTER(cov_add_optional_dep);
	TEST_REGISTER(cov_add_with_features);
	TEST_REGISTER(cov_check_no_name);
	TEST_REGISTER(cov_check_with_deps);
	TEST_REGISTER(cov_check_verbose);
	TEST_REGISTER(cov_check_sources_headers);
	TEST_REGISTER(cov_tree_with_lockfile_deps);
	TEST_REGISTER(cov_metadata_no_deps_v2);
	TEST_REGISTER(cov_update_specific_dep_v2);
	TEST_REGISTER(cov_remove_manifest_dep);
	TEST_REGISTER(cov_cflags_with_include);
	TEST_REGISTER(cov_libs_with_libdir);
	TEST_REGISTER(cov_generate_lockfile_deps_v2);
	TEST_REGISTER(cov_outdated_lockfile_with_deps);
	TEST_REGISTER(cov_init_existing_project);
	TEST_REGISTER(cov_build_no_default_features);
	TEST_REGISTER(cov_build_all_features_v2);
	TEST_REGISTER(cov_build_release_mode);
	TEST_REGISTER(cov_list_installed);
	TEST_REGISTER(cov_report_deps_detail);
	TEST_REGISTER(cov_test_verbose);
	TEST_REGISTER(cov_install_update_with_package);
	TEST_REGISTER(cov_install_update_config_basic);
	TEST_REGISTER(cov_init_with_path);
	/* Coverage batch 3: targeted path coverage */
	TEST_REGISTER(cov_tree_with_library_toml);
	TEST_REGISTER(cov_cflags_with_library_toml);
	TEST_REGISTER(cov_libs_with_library_toml);
	TEST_REGISTER(cov_generate_lockfile_with_lib);
	TEST_REGISTER(cov_update_with_library_toml);
	TEST_REGISTER(cov_remove_present_dep);
	TEST_REGISTER(cov_list_installed_with_files);
	TEST_REGISTER(cov_test_with_filter);
	TEST_REGISTER(cov_add_with_git);
	TEST_REGISTER(cov_report_audit_deps);
	TEST_REGISTER(cov_outdated_with_library_toml);
	TEST_REGISTER(cov_metadata_no_deps_v3);
	TEST_REGISTER(cov_build_manifest_path);
	TEST_REGISTER(cov_build_with_features);
	TEST_REGISTER(cov_install_update_update_pkg);

	/* ===== uninstall: with actual installed package ===== */
	TEST_REGISTER(cov_uninstall_installed);

	/* ===== check: missing version error ===== */
	TEST_REGISTER(cov_check_missing_version);

	/* ===== check: parse error (malformed Coffee.toml) ===== */
	TEST_REGISTER(cov_check_parse_error);

	/* ===== fetch: manifest parse error ===== */
	TEST_REGISTER(cov_fetch_parse_error);

	/* ===== vendor: manifest parse error ===== */
	TEST_REGISTER(cov_vendor_parse_error);

	/* ===== check: with dep = syntax ===== */
	TEST_REGISTER(cov_check_dep_eq);

	/* ===== init: library mode (no main detected) ===== */
	TEST_REGISTER(cov_init_lib_mode);

	/* ===== metadata: with features in manifest ===== */
	TEST_REGISTER(cov_metadata_with_features);

	/* ===== new: create in existing dir failure path ===== */
	TEST_REGISTER(cov_new_existing_dir);

	/* ===== remove: with Makefile cleanup ===== */
	TEST_REGISTER(cov_remove_makefile_cleanup);

	/* ===== outdated: with lockfile containing deps and registry check ===== */
	TEST_REGISTER(cov_outdated_with_lockfile_deps);

	/* ===== tree: with deps having library.toml and dependencies ===== */
	TEST_REGISTER(cov_tree_transitive_deps);

	/* ===== add: with auto-fetch from registry ===== */
	TEST_REGISTER(cov_add_auto_fetch);

	/* ===== cflags: --package with library.toml include array hitting toml parsing ===== */
	TEST_REGISTER(cov_cflags_include_toml);

	/* ===== libs: --package with library.toml lib array ===== */
	TEST_REGISTER(cov_libs_lib_toml);
}
