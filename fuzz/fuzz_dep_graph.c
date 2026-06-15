/**
 * Fuzz harness for dep_graph_create().
 * Writes fuzz data to a temp Coffee.toml, parses it, then resolves
 * the dependency graph in offline mode (no network).
 */
#include "dep_graph.h"
#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sds/sds.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	if (Size < 4 || Size > 65536) {
		return 0;
	}

	/* Write fuzz data to a temp file */
	char template[] = "/tmp/fuzz_depgraph_XXXXXX";
	i64  fd         = (i64)mkstemp(template);
	if (fd < 0) {
		return 0;
	}
	write((int)fd, Data, Size);
	close((int)fd);

	/* Parse as Coffee.toml, then resolve the dependency graph */
	sds         path = sdsnew(template);
	manifest_t *m    = manifest_parse(path);
	if (m != nullptr) {
		dep_graph_t *g = dep_graph_create(m, nullptr, true);
		if (g != nullptr) {
			dep_graph_free(g);
		}
		manifest_free(m);
	}
	sdsfree(path);

	unlink(template);
	return 0;
}
