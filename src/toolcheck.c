#include "toolcheck.h"

#include "build.h"
#include "safe.h"
#include "strings.h"

#include <stdbool.h>

#include <stdlib.h>
#include <string.h>

#include <sds/sds.h>
#include <unistd.h>

static bool tool_exists(const char *name)
{
	sds   my_name = sdsnew(name);
	char *argv[]  = { my_name, "--version", nullptr };
	bool  result  = (run_command(argv, RUN_CMD_QUIET | RUN_CMD_NOOUT) == 0);
	sdsfree(my_name);
	return result;
}

bool toolcheck_on_path(const char *name)
{
	/* Tool names come from third-party recipes; a name with a '/' or a
	 * leading '.' could probe outside the PATH directories. */
	if (!dep_name_is_valid(name)) {
		return false;
	}
	const char *path = getenv("PATH");
	if (path == nullptr) {
		return false;
	}
	bool        found = false;
	const char *start = path;
	while (!found) {
		const char *end = strchr(start, ':');
		size_t      len = end != nullptr ? (size_t)(end - start) : strlen(start);
		/* An empty PATH entry means the current directory. */
		sds         candidate = len == 0 ? sdsnew(".") : sdsnewlen(start, len);
		candidate             = sdscatprintf(candidate, "/%s", name);
		found                 = safe_access(candidate, X_OK) == 0;
		sdsfree(candidate);
		if (end == nullptr) {
			break;
		}
		start = end + 1;
	}
	return found;
}

/* Hard tool requirements, per command.  clang covers the compiler and
 * clang-tidy (which ships with clang). */
struct tool_requirements {
	const char *command;
	bool        needs_clang;
	bool        needs_git;
};

static const struct tool_requirements tool_requirements[] = {
	{ "build", true, false },
	{ "b", true, false },
	{ "compile", true, false },
	{ "check", true, false },
	{ "c", true, false },
	{ "test", true, false },
	{ "bench", true, false },
	{ "lint", true, false },
	{ "fix", true, false },
	{ "run", true, false },
	{ "install", true, true },
	{ "fetch", false, true },
	{ "update", false, true },
	{ "vendor", false, true },
	{ "outdated", false, true },
	{ "generate-lockfile", false, true },
	{ "install-update", false, true },
};

i64 toolcheck_run(const char *command_name)
{
	if (command_name == nullptr) {
		return 0;
	}

	/* Requirements are per command: clang is needed only by the commands
	 * that compile C code (directly, through make, or through clang-tidy),
	 * git only by the commands that clone or fetch dependencies.  A
	 * machine without clang must still be able to fetch dependencies.
	 *
	 * Commands absent from the table (help, version, config, search, ...)
	 * must work even when clang/git are absent from PATH.  doc and package
	 * self-check their own tools (doxygen, tar) with clear messages, so
	 * they are not probed here. */
	const struct tool_requirements *req = nullptr;
	for (size_t i = 0; i < sizeof(tool_requirements) / sizeof(tool_requirements[0]); i++) {
		if (strcmp(command_name, tool_requirements[i].command) == 0) {
			req = &tool_requirements[i];
			break;
		}
	}
	if (req == nullptr) {
		return 0;
	}

	bool ok = true;

	if (req->needs_clang && !tool_exists("clang")) {
		fprintf_safe(stderr, "Error: clang not found on PATH\n");
		ok = false;
	}
	if (req->needs_git && !tool_exists("git")) {
		fprintf_safe(stderr, "Error: git not found on PATH\n");
		ok = false;
	}

	static const char *optional[] = { "make", "curl", "zstd", "doxygen" };
	for (size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); i++) {
		if (!tool_exists(optional[i])) {
			fprintf_safe(stderr, "Warning: %s not found on PATH — some commands may not work\n", optional[i]);
		}
	}

	if (ok) {
		return 0;
	}
	return 1;
}
