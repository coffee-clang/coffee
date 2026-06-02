/**
 * Fuzz harness for features_resolve().
 * Writes fuzz input to a temp Coffee.toml, parses it, and resolves features.
 */

#include "coffee_features.h"
#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	if (Size < 4 || Size > 65536) {
		return 0;
	}

	char template[] = "/tmp/fuzz_features_XXXXXX";
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

	sds         path = sdsnew(template);
	manifest_t *m    = manifest_parse(path);
	if (m) {
		/* Try resolving with various combinations */
		resolved_features_t *rf = features_resolve(m, nullptr, 0, true, false);
		features_free(rf);

		rf = features_resolve(m, nullptr, 0, false, false);
		features_free(rf);

		/* Try with explicit feature requests */
		sds req[] = { sdsnew("default") };
		rf = features_resolve(m, req, 1, false, false);
		features_free(rf);
		sdsfree(req[0]);

		manifest_free(m);
	}
	sdsfree(path);
	unlink(template);
	return 0;
}
