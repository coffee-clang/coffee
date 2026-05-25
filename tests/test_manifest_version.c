#include "../src/manifest.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(extract_exact_version)
{
	char *name	  = NULL;
	char *version = NULL;

	manifest_extract_dep_info("toml = \"1.0.0\"", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "toml") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, "1.0.0") == 0, "version mismatch");
	free(name);
	free(version);
	PASS();
}

TEST(extract_wildcard)
{
	char *name	  = NULL;
	char *version = NULL;

	manifest_extract_dep_info("sds = \"*\"", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "sds") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, "*") == 0, "version mismatch");
	free(name);
	free(version);
	PASS();
}

TEST(extract_range)
{
	char *name	  = NULL;
	char *version = NULL;

	manifest_extract_dep_info("json = \">=2.0\"", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "json") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, ">=2.0") == 0, "version mismatch");
	free(name);
	free(version);
	PASS();
}

TEST(extract_no_version)
{
	char *name	  = NULL;
	char *version = NULL;

	manifest_extract_dep_info("mylib", &name, &version);
	ASSERT(name != NULL, "name is NULL");
	ASSERT(strcmp(name, "mylib") == 0, "name mismatch");
	ASSERT(version != NULL, "version is NULL");
	ASSERT(strcmp(version, "*") == 0, "version should default to *");
	free(name);
	free(version);
	PASS();
}

TEST(extract_null_input)
{
	char *name	  = (char *)0xdeadbeef;
	char *version = (char *)0xdeadbeef;

	manifest_extract_dep_info(NULL, &name, &version);
	ASSERT(name == NULL, "name should be NULL for NULL entry");
	ASSERT(version == NULL, "version should be NULL for NULL entry");
	PASS();
}

void coffee_register_manifest_version_tests(void)
{
	TEST_REGISTER(extract_exact_version);
	TEST_REGISTER(extract_wildcard);
	TEST_REGISTER(extract_range);
	TEST_REGISTER(extract_no_version);
	TEST_REGISTER(extract_null_input);
}