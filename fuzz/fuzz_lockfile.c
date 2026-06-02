/**
 * Fuzz harness for lockfile_parse().
 * Writes fuzz input to a temp file and passes it through lockfile_parse().
 */

#include "lockfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	if (Size < 4 || Size > 65536) {
		return 0;
	}

	char template[] = "/tmp/fuzz_lockfile_XXXXXX";
	int  fd         = mkstemp(template);
	if (fd < 0) {
		return 0;
	}
	if (write(fd, Data, Size) < 0) {
		close(fd);
		unlink(template);
		return 0;
	}
	close(fd);

	sds        path = sdsnew(template);
	lockfile_t *lf  = lockfile_parse(path);
	if (lf) {
		lockfile_free(lf);
	}
	sdsfree(path);
	unlink(template);
	return 0;
}
