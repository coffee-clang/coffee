#include "../src/coffee_features.h"
#include "../src/manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                              \
	do {                                    \
		printf("Testing %s... ", name); \
	} while (0)
#define PASS()                    \
	do {                      \
		printf("PASS\n"); \
		tests_passed++;   \
	} while (0)
#define FAIL(msg)                          \
	do {                               \
		printf("FAIL: %s\n", msg); \
		tests_failed++;            \
	} while (0)
#define ASSERT(cond, msg)          \
	do {                       \
		if (!(cond)) {     \
			FAIL(msg); \
			return 0;  \
		}                  \
	} while (0)

static int test_feature_parse(void)
{
	TEST("feature parsing from Coffee.toml");

	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf(fp, "[package]\n");
	fprintf(fp, "name = \"test\"\n\n");
	fprintf(fp, "[features]\n");
	fprintf(fp, "json = [\"serde_json\"]\n");
	fprintf(fp, "logging = [\"log/info\"]\n");
	fprintf(fp, "default = [\"json\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");
	ASSERT(m->features_count == 3, "expected 3 features");
	ASSERT(strcmp(m->features[0].name, "default") == 0 || strcmp(m->features[0].name, "json") == 0 ||
		       strcmp(m->features[0].name, "logging") == 0,
	       "unexpected feature name");

	manifest_free(m);
	PASS();
	return 1;
}

static int test_feature_resolve(void)
{
	TEST("feature resolution");

	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf(fp, "[package]\n");
	fprintf(fp, "name = \"test\"\n\n");
	fprintf(fp, "[features]\n");
	fprintf(fp, "json = [\"serde_json\"]\n");
	fprintf(fp, "logging = [\"log/info\"]\n");
	fprintf(fp, "default = [\"json\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	const char	    *requested[] = {"json"};
	resolved_features_t *resolved	 = features_resolve(m, requested, 1, false, false);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(features_is_enabled(resolved, "test", "json"), "json should be enabled");

	features_free(resolved);
	manifest_free(m);
	PASS();
	return 1;
}

static int test_feature_resolve_all(void)
{
	TEST("all_features flag");

	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf(fp, "[package]\n");
	fprintf(fp, "name = \"test\"\n\n");
	fprintf(fp, "[features]\n");
	fprintf(fp, "json = []\n");
	fprintf(fp, "xml = []\n");
	fprintf(fp, "logging = []\n");
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
	return 1;
}

static int test_feature_default(void)
{
	TEST("default features");

	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf(fp, "[package]\n");
	fprintf(fp, "name = \"test\"\n\n");
	fprintf(fp, "[features]\n");
	fprintf(fp, "json = []\n");
	fprintf(fp, "xml = []\n");
	fprintf(fp, "default = [\"json\"]\n");
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
	return 1;
}

static int test_feature_no_default(void)
{
	TEST("--no-default-features");

	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf(fp, "[package]\n");
	fprintf(fp, "name = \"test\"\n\n");
	fprintf(fp, "[features]\n");
	fprintf(fp, "json = []\n");
	fprintf(fp, "default = [\"json\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	resolved_features_t *resolved = features_resolve(m, NULL, 0, false, true);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(!features_is_enabled(resolved, "test", "json"), "json should NOT be enabled with --no-default-features");

	features_free(resolved);
	manifest_free(m);
	PASS();
	return 1;
}

static int test_cli_parsing(void)
{
	TEST("CLI feature parsing");

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
	return 1;
}

static int test_compiler_flags(void)
{
	TEST("compiler flag generation");

	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf(fp, "[package]\n");
	fprintf(fp, "name = \"test\"\n\n");
	fprintf(fp, "[features]\n");
	fprintf(fp, "json = []\n");
	fprintf(fp, "advanced-logging = []\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	const char	    *requested[] = {"json", "advanced-logging"};
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
	return 1;
}

static int test_transitive_features(void)
{
	TEST("transitive feature resolution");

	FILE *fp = fopen("/tmp/coffee-features-test.toml", "w");
	ASSERT(fp != NULL, "could not create test file");

	fprintf(fp, "[package]\n");
	fprintf(fp, "name = \"test\"\n\n");
	fprintf(fp, "[features]\n");
	fprintf(fp, "json = [\"log/info\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-features-test.toml");
	ASSERT(m != NULL, "manifest_parse returned NULL");

	const char	    *requested[] = {"json"};
	resolved_features_t *resolved	 = features_resolve(m, requested, 1, false, false);
	ASSERT(resolved != NULL, "features_resolve returned NULL");
	ASSERT(features_is_enabled(resolved, "test", "json"), "json should be enabled");
	ASSERT(features_is_enabled(resolved, "log", "info"), "log/info should be enabled transitively");

	features_free(resolved);
	manifest_free(m);
	PASS();
	return 1;
}

int main(void)
{
	printf("=== Feature System Tests ===\n\n");

	test_feature_parse();
	test_feature_resolve();
	test_feature_resolve_all();
	test_feature_default();
	test_feature_no_default();
	test_cli_parsing();
	test_compiler_flags();
	test_transitive_features();

	printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);

	remove("/tmp/coffee-features-test.toml");

	return tests_failed > 0 ? 1 : 0;
}
