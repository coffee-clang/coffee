#include "../src/manifest.h"
#include "../src/strings.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

TEST(manifest_bin_parse_single)
{
	FILE *fp = fopen("/tmp/coffee-bin-test-single.toml", "w");
	ASSERT(fp != nullptr, "could not create test file");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"testpkg\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "\n[[bin]]\n");
	fprintf_safe(fp, "name = \"mytool\"\n");
	fprintf_safe(fp, "src = [\"src/main.c\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-bin-test-single.toml");
	ASSERT(m != nullptr, "manifest_parse returned nullptr");
	ASSERT(m->bin_count == 1, "expected 1 bin target");
	ASSERT(m->bin[0].name != nullptr, "bin name is nullptr");
	ASSERT(strcmp(m->bin[0].name, "mytool") == 0, "bin name mismatch");
	ASSERT(m->bin[0].src_count == 1, "expected 1 src file");
	ASSERT(strcmp(m->bin[0].src[0], "src/main.c") == 0, "src file mismatch");

	manifest_free(m);
	remove("/tmp/coffee-bin-test-single.toml");
	PASS();
}

TEST(manifest_bin_parse_multiple)
{
	FILE *fp = fopen("/tmp/coffee-bin-test-multi.toml", "w");
	ASSERT(fp != nullptr, "could not create test file");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"testpkg\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fprintf_safe(fp, "\n[[bin]]\n");
	fprintf_safe(fp, "name = \"tool-a\"\n");
	fprintf_safe(fp, "src = [\"src/tool_a.c\"]\n");
	fprintf_safe(fp, "\n[[bin]]\n");
	fprintf_safe(fp, "name = \"tool-b\"\n");
	fprintf_safe(fp, "src = [\"src/tool_b.c\", \"src/common.c\"]\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-bin-test-multi.toml");
	ASSERT(m != nullptr, "manifest_parse returned nullptr");
	ASSERT(m->bin_count == 2, "expected 2 bin targets");
	ASSERT(strcmp(m->bin[0].name, "tool-a") == 0, "first bin name mismatch");
	ASSERT(m->bin[0].src_count == 1, "first bin src count mismatch");
	ASSERT(strcmp(m->bin[0].src[0], "src/tool_a.c") == 0, "first bin src mismatch");
	ASSERT(strcmp(m->bin[1].name, "tool-b") == 0, "second bin name mismatch");
	ASSERT(m->bin[1].src_count == 2, "second bin src count mismatch");
	ASSERT(strcmp(m->bin[1].src[0], "src/tool_b.c") == 0, "second bin src[0] mismatch");
	ASSERT(strcmp(m->bin[1].src[1], "src/common.c") == 0, "second bin src[1] mismatch");

	manifest_free(m);
	remove("/tmp/coffee-bin-test-multi.toml");
	PASS();
}

TEST(manifest_bin_no_bin_section)
{
	FILE *fp = fopen("/tmp/coffee-bin-test-none.toml", "w");
	ASSERT(fp != nullptr, "could not create test file");
	fprintf_safe(fp, "[package]\n");
	fprintf_safe(fp, "name = \"testpkg\"\n");
	fprintf_safe(fp, "version = \"1.0.0\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse("/tmp/coffee-bin-test-none.toml");
	ASSERT(m != nullptr, "manifest_parse returned nullptr");
	ASSERT(m->bin_count == 0, "expected 0 bin targets");
	ASSERT(m->bin == nullptr, "bin should be nullptr when count is 0");

	manifest_free(m);
	remove("/tmp/coffee-bin-test-none.toml");
	PASS();
}

TEST(manifest_bin_round_trip)
{
	/* Write a manifest with [[bin]], parse it, write again, re-parse and verify */
	manifest_t *m1 = calloc(1, sizeof(manifest_t));
	ASSERT(m1 != nullptr, "calloc failed");
	m1->package.name    = sdsnew("roundtrip");
	m1->package.version = sdsnew("2.0.0");
	m1->bin_count       = 1;
	m1->bin             = calloc(1, sizeof(binary_target_t));
	ASSERT(m1->bin != nullptr, "calloc for bin failed");
	m1->bin[0].name      = sdsnew("roundtool");
	m1->bin[0].src_count = 2;
	m1->bin[0].src       = calloc(2, sizeof(sds));
	m1->bin[0].src[0]    = sdsnew("src/main.c");
	m1->bin[0].src[1]    = sdsnew("src/helper.c");

	i64 ret = manifest_write("/tmp/coffee-bin-roundtrip.toml", m1);
	ASSERT(ret == 0, "manifest_write returned nonzero");
	manifest_free(m1);

	manifest_t *m2 = manifest_parse("/tmp/coffee-bin-roundtrip.toml");
	ASSERT(m2 != nullptr, "re-parse returned nullptr");
	ASSERT(m2->bin_count == 1, "re-parse bin_count mismatch");
	ASSERT(strcmp(m2->bin[0].name, "roundtool") == 0, "re-parse bin name mismatch");
	ASSERT(m2->bin[0].src_count == 2, "re-parse src_count mismatch");
	ASSERT(strcmp(m2->bin[0].src[0], "src/main.c") == 0, "re-parse src[0] mismatch");
	ASSERT(strcmp(m2->bin[0].src[1], "src/helper.c") == 0, "re-parse src[1] mismatch");

	manifest_free(m2);
	remove("/tmp/coffee-bin-roundtrip.toml");
	PASS();
}

void coffee_register_manifest_bin_tests(void)
{
	TEST_REGISTER(manifest_bin_parse_single);
	TEST_REGISTER(manifest_bin_parse_multiple);
	TEST_REGISTER(manifest_bin_no_bin_section);
	TEST_REGISTER(manifest_bin_round_trip);
}
