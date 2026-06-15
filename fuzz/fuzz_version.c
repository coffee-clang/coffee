/**
 * Fuzz harness for version_parse() and version_satisfies().
 * Feeds raw bytes as version strings to the parser.
 */
#include "version.h"

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	/* Skip empty or excessively long inputs */
	if (Size < 1 || Size > 4096) {
		return 0;
	}

	/* Null-terminate the fuzz data */
	char *buf = malloc(Size + 1);
	if (buf == nullptr) {
		return 0;
	}
	memcpy(buf, Data, Size);
	buf[Size] = '\0';

	version_t v;
	version_parse(buf, &v);

	/* Exercise constraint parser with both valid and fuzz inputs */
	version_satisfies("1.0.0", buf);
	version_satisfies(buf, "*");
	version_satisfies(buf, buf);

	free(buf);
	return 0;
}
