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

void coffee_register_doc_tests(void);

TEST(doc_generate_doxyfile_with_doc_section)
{
	/* Use a unique temp dir */
	const char *test_dir = "/tmp/coffee-doc-test-docsec";
	mkdir(test_dir, 0755);

	/* Write Coffee.toml with [doc] section */
	FILE *fp = fopen("/tmp/coffee-doc-test-docsec/Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"doctest\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "description = \"Test package for doc\"\n");
	fprintf_safe(fp, "\n[doc]\n");
	fprintf_safe(fp, "project-name = \"DocTest\"\n");
	fprintf_safe(fp, "output-dir = \"docs/api\"\n");
	fprintf_safe(fp, "input-dirs = \"src include tests\"\n");
	fclose(fp);

	/* Save CWD, chdir to test dir */
	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	i64 chdir_ok = (chdir(test_dir) == 0);
	ASSERT(chdir_ok, "chdir failed");

	/* Run doc command */
	options opt = {
		.inputs     = (char *[]){ "doc" },
		.inputs_num = 1,
	};
	(void)handle_doc(&opt);

	/* Read the generated Doxyfile */
	i64  has_doxyfile = 0;
	char buf[4'096]   = { 0 };
	fp                = fopen("Doxyfile", "r");
	if (fp) {
		size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
		buf[n]   = '\0';
		fclose(fp);
		has_doxyfile = 1;
	}

	/* Cleanup first, then assert */
	test_remove_tree("doc");
	remove("Doxyfile");
	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(test_dir);

	ASSERT(has_doxyfile, "Doxyfile should exist");
	ASSERT(strstr(buf, "OUTPUT_DIRECTORY       = doc") != nullptr, "Doxyfile should write output to doc/");
	ASSERT(strstr(buf, "docs/api") == nullptr, "Doxyfile should ignore [doc] output-dir");
	PASS();
}

TEST(doc_no_doc_section)
{
	const char *test_dir = "/tmp/coffee-doc-test-nodoc";
	mkdir(test_dir, 0755);

	FILE *fp = fopen("/tmp/coffee-doc-test-nodoc/Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"nodoc\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fclose(fp);

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	ASSERT(chdir(test_dir) == 0, "chdir failed");

	options opt = {
		.inputs     = (char *[]){ "doc" },
		.inputs_num = 1,
	};
	i64 ret = handle_doc(&opt);

	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(test_dir);

	ASSERT(ret == 0, "doc with no [doc] section should return 0");
	PASS();
}

TEST(doc_warns_on_output_dir)
{
	const char *test_dir = "/tmp/coffee-doc-test-warn";
	mkdir(test_dir, 0755);

	FILE *fp = fopen("/tmp/coffee-doc-test-warn/Coffee.toml", "w");
	ASSERT(fp != nullptr, "could not create Coffee.toml");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"warntest\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "\n[doc]\n");
	fprintf_safe(fp, "output-dir = \"docs/api\"\n");
	fprintf_safe(fp, "input-dirs = \"src\"\n");
	fclose(fp);

	char old_cwd[4'096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	ASSERT(chdir(test_dir) == 0, "chdir failed");

	/* Capture stderr to check the deprecation warning */
	FILE *old_stderr = stderr;
	FILE *capture    = fopen("stderr.txt", "w");
	ASSERT(capture != nullptr, "could not open stderr capture file");
	stderr = capture;

	options opt = {
		.inputs     = (char *[]){ "doc" },
		.inputs_num = 1,
	};
	(void)handle_doc(&opt);

	fclose(capture);
	stderr = old_stderr;

	char buf[4'096] = { 0 };
	fp              = fopen("stderr.txt", "r");
	if (fp) {
		size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
		buf[n]   = '\0';
		fclose(fp);
	}

	/* Cleanup first, then assert */
	test_remove_tree("doc");
	remove("Doxyfile");
	remove("stderr.txt");
	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(test_dir);

	ASSERT(strstr(buf, "output-dir is ignored") != nullptr, "should warn that [doc] output-dir is ignored");
	PASS();
}

void coffee_register_doc_tests(void)
{
	TEST_REGISTER(doc_generate_doxyfile_with_doc_section);
	TEST_REGISTER(doc_no_doc_section);
	TEST_REGISTER(doc_warns_on_output_dir);
}
