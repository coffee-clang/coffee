/*
 * Coverage tests for cmdline.c — comprehensive option parsing coverage.
 *
 * Tests the hand-written cmdline_parser with nearly all options
 * to exercise every option handler in the switch statement.
 */

#include "../src/cmdline.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void coffee_register_cmdline2_tests(void);

/* ---------- init / free with nullptr ---------- */
TEST(cov_cli_init_null)
{
	cmdline_parser_init(nullptr);
	/* no crash = pass */
	PASS();
}

TEST(cov_cli_free_null)
{
	cmdline_parser_free(nullptr);
	PASS();
}

/* ---------- print_help / print_version (don't exit) ---------- */
TEST(cov_cli_print_help)
{
	cmdline_parser_print_help();
	PASS();
}

TEST(cov_cli_print_version)
{
	cmdline_parser_print_version();
	PASS();
}

/* ---------- short option -q (quiet) ---------- */
TEST(cov_cli_short_quiet)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"-q", (char *)"build", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.quiet_given, "quiet flag set");
	ASSERT(args.quiet_flag, "quiet_flag true");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- short option -j (jobs) with valid number ---------- */
TEST(cov_cli_short_jobs)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"-j", (char *)"8", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.jobs_given, "jobs flag set");
	ASSERT(args.jobs_arg == 8, "jobs = 8");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- short option -p (package) ---------- */
TEST(cov_cli_short_package)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"-p", (char *)"mypkg", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.package_given, "package flag set");
	ASSERT(strcmp(args.package_arg, "mypkg") == 0, "package = mypkg");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- short option -Z (unstable-flags) ---------- */
TEST(cov_cli_short_unstable)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"-Z", (char *)"unstable-opt", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.unstable_flags_given, "unstable flags set");
	ASSERT(strcmp(args.unstable_flags_arg, "unstable-opt") == 0, "unstable-opt");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- short option -V (pkg-version) ---------- */
TEST(cov_cli_short_pkg_version)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"-V", (char *)"2.0.0", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.pkg_version_given, "pkg-version set");
	ASSERT(strcmp(args.pkg_version_arg, "2.0.0") == 0, "version = 2.0.0");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- --toolchain exact match ---------- */
TEST(cov_cli_toolchain_exact)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"--toolchain", (char *)"+stable", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.toolchain_given, "toolchain given");
	ASSERT(args.toolchain_arg == 0, "toolchain = +stable");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- --toolchain prefix match ---------- */
TEST(cov_cli_toolchain_prefix)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"--toolchain", (char *)"+st", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.toolchain_given, "toolchain given");
	ASSERT(args.toolchain_arg == 0, "toolchain = +stable (by prefix)");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- --toolchain ambiguous ---------- */
TEST(cov_cli_toolchain_ambiguous)
{
	struct cli_args args;
	/* "+" alone matches all three (+stable, +clang-stable, +gcc-stable) */
	char           *argv[] = { (char *)"coffee", (char *)"--toolchain", (char *)"+", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret != 0, "ambiguous toolchain should fail");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- --toolchain invalid value ---------- */
TEST(cov_cli_toolchain_invalid)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"--toolchain", (char *)"+nonexistent", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret != 0, "invalid toolchain should fail");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- long boolean options — one bulk test ---------- */
TEST(cov_cli_long_bool_options)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee",
		                       (char *)"--debug",
		                       (char *)"--release",
		                       (char *)"--locked",
		                       (char *)"--offline",
		                       (char *)"--frozen",
		                       (char *)"--all-features",
		                       (char *)"--no-default-features",
		                       (char *)"--unit-graph",
		                       (char *)"--workspace",
		                       (char *)"--lib",
		                       (char *)"--keep-going",
		                       (char *)"--bins",
		                       (char *)"--examples",
		                       (char *)"--tests",
		                       (char *)"--benches",
		                       (char *)"--all-targets",
		                       (char *)"--no-run",
		                       (char *)"--no-fail-fast",
		                       (char *)"--fix",
		                       (char *)"--dry-run",
		                       (char *)"--build-plan",
		                       (char *)"--dev",
		                       (char *)"--build",
		                       (char *)"--optional",
		                       (char *)"--no-optional",
		                       nullptr };
	i64             ret    = cmdline_parser(26, argv, &args);
	ASSERT(ret == 0, "parser ok");

	ASSERT(args.debug_given, "--debug");
	ASSERT(args.debug_flag, "--debug flag");
	ASSERT(args.release_given, "--release");
	ASSERT(args.release_flag, "--release flag");
	ASSERT(args.locked_given, "--locked");
	ASSERT(args.locked_flag, "--locked flag");
	ASSERT(args.offline_given, "--offline");
	ASSERT(args.offline_flag, "--offline flag");
	ASSERT(args.frozen_given, "--frozen");
	ASSERT(args.frozen_flag, "--frozen flag");
	ASSERT(args.all_features_given, "--all-features");
	ASSERT(args.all_features_flag, "--all-features flag");
	ASSERT(args.no_default_features_given, "--no-default-features");
	ASSERT(args.no_default_features_flag, "--no-default-features flag");
	ASSERT(args.unit_graph_given, "--unit-graph");
	ASSERT(args.unit_graph_flag, "--unit-graph flag");
	ASSERT(args.workspace_given, "--workspace");
	ASSERT(args.workspace_flag, "--workspace flag");
	ASSERT(args.lib_given, "--lib");
	ASSERT(args.lib_flag, "--lib flag");
	ASSERT(args.keep_going_given, "--keep-going");
	ASSERT(args.keep_going_flag, "--keep-going flag");
	ASSERT(args.bins_given, "--bins");
	ASSERT(args.bins_flag, "--bins flag");
	ASSERT(args.examples_given, "--examples");
	ASSERT(args.examples_flag, "--examples flag");
	ASSERT(args.tests_given, "--tests");
	ASSERT(args.tests_flag, "--tests flag");
	ASSERT(args.benches_given, "--benches");
	ASSERT(args.benches_flag, "--benches flag");
	ASSERT(args.all_targets_given, "--all-targets");
	ASSERT(args.all_targets_flag, "--all-targets flag");
	ASSERT(args.no_run_given, "--no-run");
	ASSERT(args.no_run_flag, "--no-run flag");
	ASSERT(args.no_fail_fast_given, "--no-fail-fast");
	ASSERT(args.no_fail_fast_flag, "--no-fail-fast flag");
	ASSERT(args.fix_given, "--fix");
	ASSERT(args.fix_flag, "--fix flag");
	ASSERT(args.dry_run_given, "--dry-run");
	ASSERT(args.dry_run_flag, "--dry-run flag");
	ASSERT(args.build_plan_given, "--build-plan");
	ASSERT(args.build_plan_flag, "--build-plan flag");
	ASSERT(args.dev_given, "--dev");
	ASSERT(args.dev_flag, "--dev flag");
	ASSERT(args.build_given, "--build");
	ASSERT(args.build_flag, "--build flag");
	ASSERT(args.optional_given, "--optional");
	ASSERT(args.optional_flag, "--optional flag");
	ASSERT(args.no_optional_given, "--no-optional");
	ASSERT(args.no_optional_flag, "--no-optional flag");

	cmdline_parser_free(&args);
	PASS();
}

/* ---------- long string options — batch 1 ---------- */
TEST(cov_cli_str_opts_1)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee",
		                       (char *)"--color",
		                       (char *)"always",
		                       (char *)"--message-format",
		                       (char *)"json",
		                       (char *)"--manifest-path",
		                       (char *)"/tmp/Coffee.toml",
		                       (char *)"--target",
		                       (char *)"x86_64",
		                       nullptr };
	i64             ret    = cmdline_parser(9, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.color_given, "--color");
	ASSERT(strcmp(args.color_arg, "always") == 0, "color=always");
	ASSERT(args.message_format_given, "--message-format");
	ASSERT(strcmp(args.message_format_arg, "json") == 0, "fmt=json");
	ASSERT(args.manifest_path_given, "--manifest-path");
	ASSERT(strcmp(args.manifest_path_arg, "/tmp/Coffee.toml") == 0, "manifest");
	ASSERT(args.target_given, "--target");
	ASSERT(strcmp(args.target_arg, "x86_64") == 0, "target");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- long string options — batch 2 ---------- */
TEST(cov_cli_str_opts_2)
{
	struct cli_args args;
	char *argv[] = { (char *)"coffee",     (char *)"--bin", (char *)"mybin",     (char *)"--example", (char *)"myex",
		             (char *)"--features", (char *)"feat1", (char *)"--profile", (char *)"release",   nullptr };
	i64   ret    = cmdline_parser(9, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.bin_given, "--bin");
	ASSERT(strcmp(args.bin_arg, "mybin") == 0, "bin");
	ASSERT(args.example_given, "--example");
	ASSERT(strcmp(args.example_arg, "myex") == 0, "example");
	ASSERT(args.features_given, "--features");
	ASSERT(strcmp(args.features_arg, "feat1") == 0, "features");
	ASSERT(args.profile_given, "--profile");
	ASSERT(strcmp(args.profile_arg, "release") == 0, "profile");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- long string options — batch 3 ---------- */
TEST(cov_cli_str_opts_3)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"--target-dir", (char *)"/tmp/build", (char *)"--timings",
		                       (char *)"info",   (char *)"--exclude",    (char *)"pkg1",       (char *)"--include",
		                       (char *)"pkg2",   (char *)"--config",     (char *)"key=val",    nullptr };
	i64             ret    = cmdline_parser(11, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.target_dir_given, "--target-dir");
	ASSERT(strcmp(args.target_dir_arg, "/tmp/build") == 0, "target-dir");
	ASSERT(args.timings_given, "--timings");
	ASSERT(strcmp(args.timings_arg, "info") == 0, "timings");
	ASSERT(args.exclude_given, "--exclude");
	ASSERT(strcmp(args.exclude_arg, "pkg1") == 0, "exclude");
	ASSERT(args.include_given, "--include");
	ASSERT(strcmp(args.include_arg, "pkg2") == 0, "include");
	ASSERT(args.config_given, "--config");
	ASSERT(strcmp(args.config_arg, "key=val") == 0, "config");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- long string options — batch 4 ---------- */
TEST(cov_cli_str_opts_4)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee",
		                       (char *)"--rename",
		                       (char *)"newname",
		                       (char *)"--path",
		                       (char *)"/some/path",
		                       (char *)"--git",
		                       (char *)"https://example.com/repo",
		                       (char *)"--branch",
		                       (char *)"main",
		                       (char *)"--tag",
		                       (char *)"v1.0",
		                       (char *)"--rev",
		                       (char *)"abc123",
		                       nullptr };
	i64             ret    = cmdline_parser(13, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.rename_given, "--rename");
	ASSERT(strcmp(args.rename_arg, "newname") == 0, "rename");
	ASSERT(args.path_given, "--path");
	ASSERT(strcmp(args.path_arg, "/some/path") == 0, "path");
	ASSERT(args.git_given, "--git");
	ASSERT(strcmp(args.git_arg, "https://example.com/repo") == 0, "git");
	ASSERT(args.branch_given, "--branch");
	ASSERT(strcmp(args.branch_arg, "main") == 0, "branch");
	ASSERT(args.tag_given, "--tag");
	ASSERT(strcmp(args.tag_arg, "v1.0") == 0, "tag");
	ASSERT(args.rev_given, "--rev");
	ASSERT(strcmp(args.rev_arg, "abc123") == 0, "rev");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- long string options — batch 5 ---------- */
TEST(cov_cli_str_opts_5)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee",   (char *)"--registry", (char *)"myreg", (char *)"--out-dir",
		                       (char *)"/tmp/out", (char *)"--command",  (char *)"test",  nullptr };
	i64             ret    = cmdline_parser(7, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.registry_given, "--registry");
	ASSERT(strcmp(args.registry_arg, "myreg") == 0, "registry");
	ASSERT(args.out_dir_given, "--out-dir");
	ASSERT(strcmp(args.out_dir_arg, "/tmp/out") == 0, "out-dir");
	ASSERT(args.command_given, "--command");
	ASSERT(strcmp(args.command_arg, "test") == 0, "command");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- --jobs invalid value ---------- */
TEST(cov_cli_jobs_invalid)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"--jobs", (char *)"abc", nullptr };
	i64             ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret != 0, "invalid jobs should fail");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- unknown long option ---------- */
TEST(cov_cli_unknown_long)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"--nonexistent-option", nullptr };
	i64             ret    = cmdline_parser(2, argv, &args);
	ASSERT(ret != 0, "unknown option should fail");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- unknown short option ---------- */
TEST(cov_cli_unknown_short)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"-X", nullptr };
	i64             ret    = cmdline_parser(2, argv, &args);
	ASSERT(ret != 0, "unknown -X should fail");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- cmdline_parser with null args_info ---------- */
TEST(cov_cli_null_args)
{
	i64 ret = cmdline_parser(1, nullptr, nullptr);
	ASSERT(ret != 0, "null args_info should fail");
	PASS();
}

/* ---------- --future-incompat-report ---------- */
TEST(cov_cli_future_incompat)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", (char *)"--future-incompat-report", nullptr };
	i64             ret    = cmdline_parser(2, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.future_incompat_report_given, "--future-incompat-report");
	ASSERT(args.future_incompat_report_flag, "flag set");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- Multiple positional args with combined options ---------- */
TEST(cov_cli_mixed_positional)
{
	struct cli_args args;
	char *argv[] = { (char *)"coffee", (char *)"--release", (char *)"add", (char *)"dep1", (char *)"dep2", nullptr };
	i64   ret    = cmdline_parser(5, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.release_given, "--release set");
	ASSERT(args.inputs_num == 3, "three inputs");
	ASSERT(strcmp(args.inputs[0], "add") == 0, "command=add");
	ASSERT(strcmp(args.inputs[1], "dep1") == 0, "dep1");
	ASSERT(strcmp(args.inputs[2], "dep2") == 0, "dep2");
	cmdline_parser_free(&args);
	PASS();
}

/* ---------- No arguments at all ---------- */
TEST(cov_cli_no_args)
{
	struct cli_args args;
	char           *argv[] = { (char *)"coffee", nullptr };
	i64             ret    = cmdline_parser(1, argv, &args);
	ASSERT(ret == 0, "parser ok");
	ASSERT(args.inputs_num == 0, "no inputs");
	cmdline_parser_free(&args);
	PASS();
}

void coffee_register_cmdline2_tests(void)
{
	TEST_REGISTER(cov_cli_init_null);
	TEST_REGISTER(cov_cli_free_null);
	TEST_REGISTER(cov_cli_print_help);
	TEST_REGISTER(cov_cli_print_version);
	TEST_REGISTER(cov_cli_short_quiet);
	TEST_REGISTER(cov_cli_short_jobs);
	TEST_REGISTER(cov_cli_short_package);
	TEST_REGISTER(cov_cli_short_unstable);
	TEST_REGISTER(cov_cli_short_pkg_version);
	TEST_REGISTER(cov_cli_toolchain_exact);
	TEST_REGISTER(cov_cli_toolchain_prefix);
	TEST_REGISTER(cov_cli_toolchain_ambiguous);
	TEST_REGISTER(cov_cli_toolchain_invalid);
	TEST_REGISTER(cov_cli_long_bool_options);
	TEST_REGISTER(cov_cli_str_opts_1);
	TEST_REGISTER(cov_cli_str_opts_2);
	TEST_REGISTER(cov_cli_str_opts_3);
	TEST_REGISTER(cov_cli_str_opts_4);
	TEST_REGISTER(cov_cli_str_opts_5);
	TEST_REGISTER(cov_cli_jobs_invalid);
	TEST_REGISTER(cov_cli_unknown_long);
	TEST_REGISTER(cov_cli_unknown_short);
	TEST_REGISTER(cov_cli_null_args);
	TEST_REGISTER(cov_cli_future_incompat);
	TEST_REGISTER(cov_cli_mixed_positional);
	TEST_REGISTER(cov_cli_no_args);
}
