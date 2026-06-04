/**
 * Fuzz harness for manifest_parse().
 * Writes fuzz input to a temp file and passes it through manifest_parse().
 */
#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	/* Skip too-short or too-long inputs */
	if (Size < 4 || Size > 65536) {
		return 0;
	}

	/* Write fuzz data to a temp file */
	char template[] = "/tmp/fuzz_manifest_XXXXXX";
	i64  fd         = (i64)mkstemp(template);
	if (fd < 0) {
		return 0;
	}
	write((int)fd, Data, Size);
	close((int)fd);

	/* Parse the manifest */
	sds         path = sdsnew(template);
	manifest_t *m    = manifest_parse(path);
	if (m) {
		manifest_free(m);
	}
	sdsfree(path);

	/* Clean up */
	unlink(template);
	return 0;
}
