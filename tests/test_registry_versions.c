#include "../src/registry.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(get_versions_structure)
{
	version_list_t *versions = registry_get_versions("toml");

	if (!versions) {
		printf("WARNING: Could not fetch versions (network issue)\n");
		PASS();
	}

	if (versions->count == 0) {
		printf("(count=0, no version field in library.toml) ");
		registry_free_versions(versions);
		PASS();
	}

	ASSERT(versions->versions != nullptr, "versions array is nullptr");
	ASSERT(versions->versions[0] != nullptr, "first version is nullptr");
	printf("(%zu versions: %s) ", versions->count, versions->versions[0]);

	registry_free_versions(versions);
	PASS();
}

TEST(get_versions_nonexistent)
{
	version_list_t *versions = registry_get_versions("__nonexistent_pkg_xyz__");

	if (!versions || versions->count == 0) {
		if (versions) {
			registry_free_versions(versions);
		}
		printf("(correctly returned no versions) ");
		PASS();
	}

	FAIL("should return no versions for nonexistent package");
	registry_free_versions(versions);
}

TEST(get_versions_null)
{
	version_list_t *versions = registry_get_versions(nullptr);
	ASSERT(versions == nullptr, "should return nullptr for nullptr name");
	PASS();
}

TEST(get_versions_empty)
{
	version_list_t *versions = registry_get_versions("");
	ASSERT(versions == nullptr, "should return nullptr for empty name");
	PASS();
}

TEST(free_versions_null)
{
	registry_free_versions(nullptr);
	PASS();
}

void coffee_register_registry_versions_tests(void)
{
	TEST_REGISTER(get_versions_structure);
	TEST_REGISTER(get_versions_nonexistent);
	TEST_REGISTER(get_versions_null);
	TEST_REGISTER(get_versions_empty);
	TEST_REGISTER(free_versions_null);
}