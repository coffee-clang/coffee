#include "../deps/sds/sds.h"
#include "../src/manifest.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(extract_exact_version)
{
	sds name    = nullptr;
	sds version = nullptr;

	manifest_extract_dep_info("toml = \"1.0.0\"", &name, &version);
	ASSERT(name != nullptr, "name is nullptr");
	ASSERT(strcmp(name, "toml") == 0, "name mismatch");
	ASSERT(version != nullptr, "version is nullptr");
	ASSERT(strcmp(version, "1.0.0") == 0, "version mismatch");
	sdsfree(name);
	sdsfree(version);
	PASS();
}

TEST(extract_wildcard)
{
	sds name    = nullptr;
	sds version = nullptr;

	manifest_extract_dep_info("sds = \"*\"", &name, &version);
	ASSERT(name != nullptr, "name is nullptr");
	ASSERT(strcmp(name, "sds") == 0, "name mismatch");
	ASSERT(version != nullptr, "version is nullptr");
	ASSERT(strcmp(version, "*") == 0, "version mismatch");
	sdsfree(name);
	sdsfree(version);
	PASS();
}

TEST(extract_range)
{
	sds name    = nullptr;
	sds version = nullptr;

	manifest_extract_dep_info("json = \">=2.0\"", &name, &version);
	ASSERT(name != nullptr, "name is nullptr");
	ASSERT(strcmp(name, "json") == 0, "name mismatch");
	ASSERT(version != nullptr, "version is nullptr");
	ASSERT(strcmp(version, ">=2.0") == 0, "version mismatch");
	sdsfree(name);
	sdsfree(version);
	PASS();
}

TEST(extract_no_version)
{
	sds name    = nullptr;
	sds version = nullptr;

	manifest_extract_dep_info("mylib", &name, &version);
	ASSERT(name != nullptr, "name is nullptr");
	ASSERT(strcmp(name, "mylib") == 0, "name mismatch");
	ASSERT(version != nullptr, "version is nullptr");
	ASSERT(strcmp(version, "*") == 0, "version should default to *");
	sdsfree(name);
	sdsfree(version);
	PASS();
}

TEST(extract_null_input)
{
	sds name    = (sds)0xdeadbeef;
	sds version = (sds)0xdeadbeef;

	manifest_extract_dep_info(nullptr, &name, &version);
	ASSERT(name == nullptr, "name should be nullptr for nullptr entry");
	ASSERT(version == nullptr, "version should be nullptr for nullptr entry");
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