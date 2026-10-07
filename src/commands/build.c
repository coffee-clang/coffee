#include "../build.h"

#include "../coffee.h"
#include "../manifest.h"
#include "../project.h"
#include "safe.h"

#include <stdio.h>
#include <string.h>

int64_t handle_build(options *opts)
{
	/* The final positional argument selects the make target;
	 * a bare `coffee build` builds the default target `all`. */
	const char *target = "all";
	if (opts->inputs_num > 1) {
		target = opts->inputs[opts->inputs_num - 1];
	}

	/* The target is passed to make verbatim, so validate it. */
	if (!build_target_is_valid(target)) {
		fprintf_safe(stderr, "Error: invalid build target '%s'\n", target);
		return 1;
	}

	return build_project(opts, target, nullptr);
}
