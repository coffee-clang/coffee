#include "toolcheck.h"

#include "build.h"
#include "strings.h"

#include <stdbool.h>

#include <string.h>

#include <sds/sds.h>

static bool tool_exists(const char *name)
{
	sds   my_name = sdsnew(name);
	char *argv[]  = { my_name, "--version", nullptr };
	bool  result  = (run_command(argv, RUN_CMD_QUIET) == 0);
	sdsfree(my_name);
	return result;
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
