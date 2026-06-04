/*
 * Coverage tests for cmdline.c (CLI argument parser).
 *
 * Tests the cmdline_parser function directly with various argument
 * combinations to exercise the getopt_long dispatch.
 */

#include "../src/cmdline.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void coffee_register_cmdline_tests(void);

TEST(cov_cmdline_verbose)
{
	struct cli_args args;

	char *argv[] = { (char *)"coffee", (char *)"-v", (char *)"build", nullptr };
	i64   ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "cmdline_parser -v should succeed");
	ASSERT(args.verbose_given, "verbose flag should be set");

	cmdline_parser_free(&args);
	PASS();
}

TEST(cov_cmdline_subcommand)
{
	struct cli_args args;

	char *argv[] = { (char *)"coffee", (char *)"build", (char *)"--release", nullptr };
	i64   ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "cmdline_parser build --release");
	ASSERT(args.inputs_num >= 1, "should have inputs");
	ASSERT(strcmp(args.inputs[0], "build") == 0, "first input is build");
	ASSERT(args.release_given, "release flag should be set");

	cmdline_parser_free(&args);
	PASS();
}

TEST(cov_cmdline_multiple_inputs)
{
	struct cli_args args;

	char *argv[] = { (char *)"coffee", (char *)"add", (char *)"some-package", nullptr };
	i64   ret    = cmdline_parser(3, argv, &args);
	ASSERT(ret == 0, "cmdline_parser add some-package");
	ASSERT(args.inputs_num == 2, "two inputs (add, some-package)");
	ASSERT(strcmp(args.inputs[0], "add") == 0, "first input is add");
	ASSERT(strcmp(args.inputs[1], "some-package") == 0, "second input is package");

	cmdline_parser_free(&args);
	PASS();
}

void coffee_register_cmdline_tests(void)
{
	TEST_REGISTER(cov_cmdline_verbose);
	TEST_REGISTER(cov_cmdline_subcommand);
	TEST_REGISTER(cov_cmdline_multiple_inputs);
}
