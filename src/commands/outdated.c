#include "../coffee.h"
#include "../dep_graph.h"
#include "../lockfile.h"
#include "../manifest.h"
#include "../project.h"
#include "../recipe_install.h"
#include "../registry.h"
#include "../version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Compare an installed registry dependency with the registry's current
 * recipe.  Returns the status text (caller frees) and sets *outdated.
 */
static sds recipe_status_text(const char *name, const char *installed, bool offline, bool *outdated)
{
	*outdated = false;
	if (offline) {
		return sdsnew("(offline)");
	}
	sds       key = sdsnew(name);
	recipe_t *r   = registry_get(key);
	sdsfree(key);
	if (r == nullptr || r->version == nullptr) {
		registry_free_recipe(r);
		return sdsnew("? (registry unreachable)");
	}
	version_t have;
	version_t latest;
	if (version_parse(installed, &have) && version_parse(r->version, &latest)) {
		*outdated = version_cmp(&latest, &have) > 0;
	} else {
		*outdated = strcmp(installed, r->version) != 0;
	}
	sds text = *outdated ? sdscatprintf(sdsempty(), "Outdated (registry has %s)", r->version) : sdsnew("Up-to-date");
	registry_free_recipe(r);
	return text;
}

int64_t handle_outdated(options *opts)
{
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
		printf_safe("No dependencies in lockfile.\n");
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

	printf_safe("%-20s %-15s %s\n", "PACKAGE", "PINNED", "STATUS");
	printf_safe("%-20s %-15s %s\n", "-------", "------", "------");

	i64 outdated_count = 0;

	for (size_t i = 0; i < lf->deps_count; i++) {
		const char *pkg = lf->deps[i].name ? lf->deps[i].name : "(unknown)";

		struct recipe_info installed;
		if (lf->deps[i].checksum != nullptr && lf->deps[i].path != nullptr &&
		    recipe_read_installed(lf->deps[i].path, &installed)) {
			bool outdated = false;
			sds  status   = recipe_status_text(pkg, installed.version, opts->offline, &outdated);
			printf_safe("%-20s %-15s %s\n", pkg, installed.version, status);
			if (outdated) {
				outdated_count++;
			}
			sdsfree(status);
			recipe_info_free(&installed);
			continue;
		}

		if (!dep_graph_is_git(g, pkg)) {
			/* Non-git deps: show pinned version but no remote check */
			const char *pinned = lf->deps[i].version ? lf->deps[i].version : "*";
			printf_safe("%-20s %-15s %s\n", pkg, pinned, "(not a git dep)");
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

		printf_safe("%-20s %-15s %s\n", pkg, pinned_short, status);
		sdsfree(behind_str);
	}

	printf_safe("\n");
	if (outdated_count > 0) {
		printf_safe("%lld %s outdated.\n", (long long)outdated_count,
		            outdated_count == 1 ? "dependency is" : "dependencies are");
	} else {
		printf_safe("All dependencies are up-to-date.\n");
	}

	dep_graph_free(g);
	lockfile_free(lf);
	manifest_free(m);
	return outdated_count > 0 ? 1 : 0;
}
