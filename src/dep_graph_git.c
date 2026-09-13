#include "dep_graph.h"

#include "build.h"
#include "safe.h"
#include "strings.h"

#include <sds/sds.h>

/* ------------------------------------------------------------------ */
/* Git operations on resolved dependency nodes                          */
/* ------------------------------------------------------------------ */

i64 dep_graph_compare_remote(const dep_graph_t *g, const char *dep_name, sds *behind_by)
{
	if (g == nullptr || dep_name == nullptr) {
		return -1;
	}
	i64 idx = dep_graph_find_node(g, dep_name);
	if (idx < 0 || !g->nodes[idx].is_git || g->nodes[idx].path == nullptr) {
		return -1;
	}

	const char *dep_path = g->nodes[idx].path;
	const char *ref      = g->nodes[idx].git_ref != nullptr ? g->nodes[idx].git_ref : "origin/HEAD";

	if (ref != nullptr && !ref_is_valid(ref)) {
		if (behind_by) {
			*behind_by = sdsnew("(invalid ref)");
		}
		return -1;
	}

	if (g->offline) {
		if (behind_by) {
			*behind_by = sdsnew("(offline)");
		}
		return -1;
	}

	/* git fetch origin */
	char *fetch_argv[] = { "git", "-C", unconst(dep_path), "fetch", "origin", "--depth", "1", nullptr };
	i64   ret          = run_command(fetch_argv, RUN_CMD_QUIET);

	if (ret != 0) {
		if (behind_by) {
			*behind_by = sdsnew("(fetch failed)");
		}
		return -1;
	}

	/* git rev-list --count HEAD..<ref>  */
	sds   rev_arg    = sdscatprintf(sdsempty(), "HEAD..%s", ref);
	char *rev_argv[] = { "git", "-C", unconst(dep_path), "rev-list", "--count", rev_arg, nullptr };
	sds   output     = run_command_capture(rev_argv, RUN_CMD_QUIET);
	sdsfree(rev_arg);
	if (output == nullptr) {
		if (behind_by) {
			*behind_by = sdsnew("(rev-list failed)");
		}
		return -1;
	}

	/* Strip trailing newline */
	size_t olen = sdslen(output);
	if (olen > 0 && output[olen - 1] == '\n') {
		output[olen - 1] = '\0';
	}

	i64 behind;
	if (!safe_atol(output, &behind)) {
		behind = 0;
	}
	sdsfree(output);
	if (behind_by) {
		if (behind == 0) {
			*behind_by = sdsnew("up-to-date");
		} else if (behind == 1) {
			*behind_by = sdsnew("1 commit behind");
		} else {
			*behind_by = sdscatprintf(sdsempty(), "%lld commits behind", (long long)behind);
		}
	}
	return behind;
}

i64 dep_graph_fetch_git(dep_graph_t *g, const char *dep_name, bool verbose)
{
	if (g == nullptr || dep_name == nullptr) {
		return -1;
	}
	i64 idx = dep_graph_find_node(g, dep_name);
	if (idx < 0 || !g->nodes[idx].is_git) {
		return -1;
	}

	const char *dep_path = g->nodes[idx].path;
	const char *ref      = g->nodes[idx].git_ref;

	if (dep_path == nullptr) {
		return -1;
	}

	if (ref != nullptr && !ref_is_valid(ref)) {
		fprintf_safe(stderr, "    Error: invalid git ref '%s' for '%s'\n", ref, dep_name);
		return -1;
	}

	i64 ret;
	if (ref != nullptr) {
		if (verbose) {
			printf_safe("    Fetching %s (%s)...\n", dep_name, ref);
		}
		/* '--' before the refspec prevents git from parsing a ref that
		 * starts with '-' as an option (option injection). */
		char *fetch_argv[] = {
			"git", "-C", unconst(dep_path), "fetch", "origin", "--depth", "1", "--", unconst(ref), nullptr
		};
		ret = run_command(fetch_argv, RUN_CMD_QUIET);
		if (ret == 0) {
			char *co_argv[] = { "git", "-C", unconst(dep_path), "checkout", unconst(ref), nullptr };
			ret             = run_command(co_argv, RUN_CMD_QUIET);
		}
	} else {
		if (verbose) {
			printf_safe("    Fetching %s...\n", dep_name);
		}
		char *fetch_argv[] = { "git", "-C", unconst(dep_path), "fetch", "--depth", "1", "origin", nullptr };
		ret                = run_command(fetch_argv, RUN_CMD_QUIET);
		if (ret == 0) {
			char *reset_argv[] = { "git", "-C", unconst(dep_path), "reset", "--hard", "origin/HEAD", nullptr };
			ret                = run_command(reset_argv, RUN_CMD_QUIET);
		}
	}

	if (ret == 0) {
		/* Update commit SHA in the node */
		char *rev_argv[] = { "git", "-C", unconst(dep_path), "rev-parse", "HEAD", nullptr };
		sds   output     = run_command_capture(rev_argv, RUN_CMD_QUIET);
		if (output != nullptr) {
			size_t olen = sdslen(output);
			if (olen > 0 && output[olen - 1] == '\n') {
				output[olen - 1] = '\0';
			}
			if (output[0] != '\0') {
				sdsfree(g->nodes[idx].commit);
				g->nodes[idx].commit = sdsnew(output);
			}
			sdsfree(output);
		}
	}
	return ret;
}
