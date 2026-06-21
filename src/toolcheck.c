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

i64 toolcheck_run(const char *command_name)
{
	if (command_name != nullptr) {
		if (strcmp(command_name, "help") == 0 || strcmp(command_name, "version") == 0) {
			return 0;
		}
	}

	struct {
		const char *name;
		const char *label;
		bool        hard;
	} tools[] = {
		{ "clang", "clang", true }, { "git", "git", true },    { "make", "make", false },
		{ "curl", "curl", false },  { "zstd", "zstd", false }, { "doxygen", "doxygen", false },
	};

	bool ok = true;

	for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
		if (!tool_exists(tools[i].name)) {
			if (tools[i].hard) {
				fprintf_safe(stderr, "Error: %s not found on PATH\n", tools[i].label);
				ok = false;
			} else {
				fprintf_safe(stderr, "Warning: %s not found on PATH — some commands may not work\n", tools[i].label);
			}
		}
	}

	if (ok) {
		return 0;
	}
	return 1;
}
