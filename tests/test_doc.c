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
	char old_cwd[4096];
	ASSERT(getcwd(old_cwd, sizeof(old_cwd)) != nullptr, "getcwd failed");
	i64 chdir_ok = (chdir(test_dir) == 0);
	ASSERT(chdir_ok, "chdir failed");

	/* Run doc command */
	options opt = {
		.inputs     = (char *[]){ "doc" },
		.inputs_num = 1,
	};
	(void)handle_doc(&opt);

	/* Check that Doxyfile was generated */
	i64 has_doxyfile = (access("Doxyfile", F_OK) == 0);

	/* Cleanup first, then assert */
	fp = fopen("Doxyfile", "r");
	if (fp) {
		char   buf[4096];
		size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
		buf[n]   = '\0';
		fclose(fp);
		remove("Doxyfile");
	}
	remove("Coffee.toml");
	chdir(old_cwd);
	rmdir(test_dir);

	ASSERT(has_doxyfile, "Doxyfile should exist");
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

	char old_cwd[4096];
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

void coffee_register_doc_tests(void)
{
	TEST_REGISTER(doc_generate_doxyfile_with_doc_section);
	TEST_REGISTER(doc_no_doc_section);
}
