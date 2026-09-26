/*
 * Tests for manifest dependency parsing: [[dependencies]] array of tables
 * and non-string flat values.
 */

#include "../src/build.h"
#include "../src/manifest.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

void coffee_register_manifest_deps_tests(void);

static char saved_cwd[4'096];

static void setup_tmpdir(const char *name)
{
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/manifest-deps-%s", name);
	mkdir(tmpdir, 0755);
	assert(getcwd(saved_cwd, sizeof(saved_cwd)) != nullptr);
	assert(chdir(tmpdir) == 0);
	sdsfree(tmpdir);
}

static void teardown_tmpdir(const char *name)
{
	chdir(saved_cwd);
	sds tmpdir = sdscatprintf(sdsempty(), "/tmp/manifest-deps-%s", name);
	test_remove_tree(tmpdir);
	sdsfree(tmpdir);
}

static void write_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");
	assert(fp);
	fprintf_safe(fp, "%s", content);
	fclose(fp);
}

/* [[dependencies]] array of tables parses into structured deps */
TEST(manifest_deps_array_of_tables)
{
	setup_tmpdir("aot");
	write_file("Coffee.toml", "[[dependencies]]\n"
	                          "name = \"libgit\"\n"
	                          "git = \"https://example.com/libgit.git\"\n"
	                          "branch = \"main\"\n"
	                          "\n"
	                          "[[dependencies]]\n"
	                          "name = \"libpath\"\n"
	                          "path = \"./libpath\"\n"
	                          "version = \"1.0.0\"\n"
	                          "optional = true\n"
	                          "\n"
	                          "[package]\n"
	                          "name = \"aot\"\n"
	                          "version = \"1.0.0\"\n"
	                          "edition = \"c23\"\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");
	ASSERT(m->dependencies.deps_count == 2, "two structured deps");

	ASSERT(strcmp(m->dependencies.deps[0].name, "libgit") == 0, "first dep name");
	ASSERT(m->dependencies.deps[0].git != nullptr &&
	           strcmp(m->dependencies.deps[0].git, "https://example.com/libgit.git") == 0,
	       "first dep git");
	ASSERT(m->dependencies.deps[0].branch != nullptr && strcmp(m->dependencies.deps[0].branch, "main") == 0,
	       "first dep branch");

	ASSERT(strcmp(m->dependencies.deps[1].name, "libpath") == 0, "second dep name");
	ASSERT(m->dependencies.deps[1].path != nullptr && strcmp(m->dependencies.deps[1].path, "./libpath") == 0,
	       "second dep path");
	ASSERT(m->dependencies.deps[1].version != nullptr && strcmp(m->dependencies.deps[1].version, "1.0.0") == 0,
	       "second dep version");
	ASSERT(m->dependencies.deps[1].optional, "second dep optional");

	/* No flat entries for array-of-tables deps */
	ASSERT(m->package.dependencies_count == 0, "no flat deps");

	manifest_free(m);
	teardown_tmpdir("aot");
	PASS();
}

/* Non-string values in [dependencies] are dropped with a warning */
TEST(manifest_deps_non_string_value)
{
	setup_tmpdir("nonstr");
	write_file("Coffee.toml", "[dependencies]\n"
	                          "good = \"1.0\"\n"
	                          "bad = 1.0\n"
	                          "\n"
	                          "[package]\n"
	                          "name = \"nonstr\"\n"
	                          "version = \"1.0.0\"\n"
	                          "edition = \"c23\"\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");
	ASSERT(m->package.dependencies_count == 1, "only the string dep kept");
	ASSERT(strcmp(m->package.dependencies[0], "good = \"1.0\"") == 0, "string dep preserved");

	manifest_free(m);
	teardown_tmpdir("nonstr");
	PASS();
}

/* Round-trip: [[dependencies]] survives manifest_write and re-parses */
TEST(manifest_deps_roundtrip)
{
	setup_tmpdir("roundtrip");
	write_file("Coffee.toml", "[[dependencies]]\n"
	                          "name = \"libgit\"\n"
	                          "git = \"https://example.com/libgit.git\"\n"
	                          "tag = \"v1.2.0\"\n"
	                          "\n"
	                          "[package]\n"
	                          "name = \"roundtrip\"\n"
	                          "version = \"1.0.0\"\n"
	                          "edition = \"c23\"\n");

	manifest_t *m = manifest_parse("Coffee.toml");
	ASSERT(m != nullptr, "parse manifest");
	ASSERT(m->dependencies.deps_count == 1, "one structured dep");

	ASSERT(manifest_write("Coffee.toml.out", m) == 0, "write manifest");

	/* The emitted form is canonical: an inline table under [dependencies]. */
	FILE *rf = fopen("Coffee.toml.out", "r");
	ASSERT(rf != nullptr, "fopen written manifest failed");
	char   buf[4'096];
	size_t rn = fread(buf, 1, sizeof(buf) - 1, rf);
	buf[rn]   = '\0';
	fclose(rf);
	ASSERT(strstr(buf, "[dependencies]") != nullptr, "emitted as a [dependencies] table");
	ASSERT(strstr(buf, "git = ") != nullptr, "git emitted as an inline-table field");

	manifest_t *m2 = manifest_parse("Coffee.toml.out");
	ASSERT(m2 != nullptr, "re-parse written manifest");
	ASSERT(m2->dependencies.deps_count == 1, "dep preserved after round-trip");
	ASSERT(strcmp(m2->dependencies.deps[0].name, "libgit") == 0, "name preserved");
	ASSERT(m2->dependencies.deps[0].git != nullptr &&
	           strcmp(m2->dependencies.deps[0].git, "https://example.com/libgit.git") == 0,
	       "git preserved");
	ASSERT(m2->dependencies.deps[0].tag != nullptr && strcmp(m2->dependencies.deps[0].tag, "v1.2.0") == 0,
	       "tag preserved");

	manifest_free(m2);
	manifest_free(m);
	teardown_tmpdir("roundtrip");
	PASS();
}

void coffee_register_manifest_deps_tests(void)
{
	TEST_REGISTER(manifest_deps_array_of_tables);
	TEST_REGISTER(manifest_deps_non_string_value);
	TEST_REGISTER(manifest_deps_roundtrip);
}