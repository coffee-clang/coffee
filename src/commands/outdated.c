#include "../coffee.h"
#include "../dep_graph.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t handle_outdated(options *opts)
{
	(void)opts;
	char *manifest_path = project_find_manifest(nullptr);
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: No manifest found in current directory\n");
		return 1;
	}

	manifest_t *m = manifest_parse(manifest_path);
	sdsfree(manifest_path);

	if (m == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		return 1;
	}

	lockfile_t *lf = lockfile_parse("Coffee.lock");
	if (lf == nullptr) {
		fprintf_safe(stderr, "No lockfile found at Coffee.lock\n");
		fprintf_safe(stderr, "Run 'coffee generate-lockfile' first\n");
		manifest_free(m);
		return 0;
	}

	if (lf->deps_count == 0) {
		printf("No dependencies in lockfile.\n");
		lockfile_free(lf);
		manifest_free(m);
		return 0;
	}

	dep_graph_t *g = dep_graph_create(m, lf, false);
	if (g == nullptr) {
		lockfile_free(lf);
		manifest_free(m);
		fprintf_safe(stderr, "Error: Could not resolve dependency graph\n");
		return 1;
	}

	printf("%-20s %-15s %s\n", "PACKAGE", "PINNED", "STATUS");
	printf("%-20s %-15s %s\n", "-------", "------", "------");

	i64 outdated_count = 0;

	for (size_t i = 0; i < lf->deps_count; i++) {
		const char *pkg = lf->deps[i].name ? lf->deps[i].name : "(unknown)";

		if (!dep_graph_is_git(g, pkg)) {
			/* Non-git deps: show pinned version but no remote check */
			const char *pinned = lf->deps[i].version ? lf->deps[i].version : "*";
			printf("%-20s %-15s %s\n", pkg, pinned, "(not a git dep)");
			continue;
		}

		sds behind_str = nullptr;
		i64 behind     = dep_graph_compare_remote(g, pkg, &behind_str);

		const char *status;
		if (behind < 0) {
			status = behind_str != nullptr ? behind_str : "?";
		} else if (behind == 0) {
			status = "Up-to-date";
		} else {
			status = behind_str != nullptr ? behind_str : "Outdated";
			outdated_count++;
		}

		/* Truncate pinned SHA to 12 characters */
		char pinned_short[16];
		if (lf->deps[i].commit) {
			size_t clen = strlen(lf->deps[i].commit);
			if (clen > 12) {
				memccpy(pinned_short, lf->deps[i].commit, '\0', 12);
				pinned_short[12] = '\0';
			} else {
				memccpy(pinned_short, lf->deps[i].commit, '\0', clen + 1);
			}
		} else {
			snprintf_safe(pinned_short, sizeof(pinned_short), "-");
		}

		printf("%-20s %-15s %s\n", pkg, pinned_short, status);
		sdsfree(behind_str);
	}

	printf("\n");
	if (outdated_count > 0) {
		printf("%lld %s outdated.\n", (long long)outdated_count,
		       outdated_count == 1 ? "dependency is" : "dependencies are");
	} else {
		printf("All dependencies are up-to-date.\n");
	}

	dep_graph_free(g);
	lockfile_free(lf);
	manifest_free(m);
	return outdated_count > 0 ? 1 : 0;
}
