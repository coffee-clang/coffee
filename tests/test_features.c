#include "../src/coffee_features.h"
#include "../src/manifest.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(feature_parse)
{
	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"test\"\n\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "json = [\"serde_json\"]\n");
	fprintf_safe(fp, "logging = [\"log/info\"]\n");
	fprintf_safe(fp, "default = [\"json\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");
	ASSERT(m->features_count == 3, "expected 3 features");
	ASSERT(strcmp(m->features[0].name, "default") == 0 || strcmp(m->features[0].name, "json") == 0 ||
			   strcmp(m->features[0].name, "logging") == 0,
		   "unexpected feature name");

	manifest_free(m);
	PASS();
}

TEST(feature_resolve)
{
	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"test\"\n\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "json = [\"serde_json\"]\n");
	fprintf_safe(fp, "logging = [\"log/info\"]\n");
	fprintf_safe(fp, "default = [\"json\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	const char			*requested[] = { "json" };
	resolved_features_t *resolved	 = features_resolve(m, requested, 1, false, false);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(features_is_enabled(resolved, "test", "json"), "json should be enabled");

	features_free(resolved);
	manifest_free(m);
	PASS();
}

TEST(feature_resolve_all)
{
	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"test\"\n\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "json = []\n");
	fprintf_safe(fp, "xml = []\n");
	fprintf_safe(fp, "logging = []\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	resolved_features_t *resolved = features_resolve(m, NULL, 0, true, false);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(features_is_enabled(resolved, "test", "json"), "json should be enabled");
	ASSERT(features_is_enabled(resolved, "test", "xml"), "xml should be enabled");
	ASSERT(features_is_enabled(resolved, "test", "logging"), "logging should be enabled");

	features_free(resolved);
	manifest_free(m);
	PASS();
}

TEST(feature_default)
{
	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"test\"\n\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "json = []\n");
	fprintf_safe(fp, "xml = []\n");
	fprintf_safe(fp, "default = [\"json\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	resolved_features_t *resolved = features_resolve(m, NULL, 0, false, false);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(features_is_enabled(resolved, "test", "json"), "json should be enabled as default");
	ASSERT(!features_is_enabled(resolved, "test", "xml"), "xml should NOT be enabled by default");

	features_free(resolved);
	manifest_free(m);
	PASS();
}

TEST(feature_no_default)
{
	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"test\"\n\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "json = []\n");
	fprintf_safe(fp, "default = [\"json\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	resolved_features_t *resolved = features_resolve(m, NULL, 0, false, true);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(!features_is_enabled(resolved, "test", "json"), "json should NOT be enabled with --no-default-features");

	features_free(resolved);
	manifest_free(m);
	PASS();
}

TEST(cli_parsing)
{
	char **features = NULL;
	size_t count	= 0;
	features_parse_cli("json,xml,logging", &features, &count);
	ASSERT(count == 3, "expected 3 features");
	ASSERT(strcmp(features[0], "json") == 0, "first feature should be json");
	ASSERT(strcmp(features[1], "xml") == 0, "second feature should be xml");
	ASSERT(strcmp(features[2], "logging") == 0, "third feature should be logging");

	for (size_t i = 0; i < count; i++) {
		free(features[i]);
	}
	free(features);

	PASS();
}

TEST(compiler_flags)
{
	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"test\"\n\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "json = []\n");
	fprintf_safe(fp, "advanced-logging = []\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	const char			*requested[] = { "json", "advanced-logging" };
	resolved_features_t *resolved	 = features_resolve(m, requested, 2, false, false);
	ASSERT(resolved != NULL, "features_resolve returned NULL");

	size_t flags_count = 0;
	char **flags	   = features_to_compiler_flags(resolved, "test", &flags_count);
	ASSERT(flags != NULL, "features_to_compiler_flags returned NULL");
	ASSERT(flags_count == 2, "expected 2 flags");

	int found_json	  = 0;
	int found_logging = 0;
	for (size_t i = 0; i < flags_count; i++) {
		if (strcmp(flags[i], "-DFEATURE_JSON") == 0) {
			found_json = 1;
		}
		if (strcmp(flags[i], "-DFEATURE_ADVANCED_LOGGING") == 0) {
			found_logging = 1;
		}
		free(flags[i]);
	}
	free(flags);

	ASSERT(found_json, "-DFEATURE_JSON not found");
	ASSERT(found_logging, "-DFEATURE_ADVANCED_LOGGING not found");

	features_free(resolved);
	manifest_free(m);
	PASS();
}

TEST(transitive_features)
{
	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"test\"\n\n");
	fprintf_safe(fp, "[features]\n");
	fprintf_safe(fp, "json = [\"log/info\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	const char			*requested[] = { "json" };
	resolved_features_t *resolved	 = features_resolve(m, requested, 1, false, false);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(features_is_enabled(resolved, "test", "json"), "json should be enabled");
	ASSERT(features_is_enabled(resolved, "log", "info"), "log/info should be enabled transitively");

	features_free(resolved);
	manifest_free(m);
	PASS();
}

void coffee_register_features_tests(void)
{
	TEST_REGISTER(feature_parse);
	TEST_REGISTER(feature_resolve);
	TEST_REGISTER(feature_resolve_all);
	TEST_REGISTER(feature_default);
	TEST_REGISTER(feature_no_default);
	TEST_REGISTER(cli_parsing);
	TEST_REGISTER(compiler_flags);
	TEST_REGISTER(transitive_features);
}