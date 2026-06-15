/**
 * Fuzz harness for registry_search().
 * Writes fuzz data as a mock packages.json index, then calls
 * registry_search() / registry_get() to exercise the JSON parser.
 */
#include "registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sds/sds.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	if (Size < 4 || Size > 65536) {
		return 0;
	}

	/* Create a temporary directory for the mock cache */
	char template[] = "/tmp/fuzz_registry_XXXXXX";
	char *tmpdir    = mkdtemp(template);
	if (tmpdir == nullptr) {
		return 0;
	}

	/* Create .coffee/ subdirectory */
	char cachedir[4096];
	snprintf(cachedir, sizeof(cachedir), "%s/.coffee", tmpdir);
	if (mkdir(cachedir, 0755) != 0) {
		rmdir(tmpdir);
		return 0;
	}

	/* Write fuzz data as packages.json */
	char indexpath[4096];
	snprintf(indexpath, sizeof(indexpath), "%s/packages.json", cachedir);
	FILE *fp = fopen(indexpath, "w");
	if (fp == nullptr) {
		rmdir(cachedir);
		rmdir(tmpdir);
		return 0;
	}
	fwrite(Data, 1, Size, fp);
	fclose(fp);

	/* Point COFFEE_HOME at the temporary tree */
	setenv("COFFEE_HOME", tmpdir, 1);

	/* Exercise the JSON parser via registry_search */
	recipe_list_t *list = registry_search(sdsempty());
	if (list != nullptr) {
		registry_free_recipes(list);
	}

	/* Also exercise registry_get (lookup a single package) */
	registry_get(sdsnew("test"));

	/* Cleanup */
	unlink(indexpath);
	rmdir(cachedir);
	rmdir(tmpdir);
	return 0;
}
