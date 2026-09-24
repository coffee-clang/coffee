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

	/* Parse the manifest, then round-trip it through manifest_write so
	 * the writer is exercised on arbitrary (possibly hostile) input. */
	sds         path = sdsnew(template);
	manifest_t *m    = manifest_parse(path);
	if (m) {
		sds out_path = sdsnew(template);
		out_path     = sdscat(out_path, ".out");
		manifest_write(out_path, m);
		manifest_t *m2 = manifest_parse(out_path);
		if (m2) {
			manifest_free(m2);
		}
		unlink(out_path);
		sdsfree(out_path);
		manifest_free(m);
	}
	sdsfree(path);

	/* Clean up */
	unlink(template);
	return 0;
}
