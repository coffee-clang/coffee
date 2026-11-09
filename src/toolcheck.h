#ifndef TOOLCHECK_H_
#define TOOLCHECK_H_

#include <stdbool.h>
#include <stdint.h>

typedef int64_t i64;

/**
 * @brief Check that required system tools exist on PATH.
 *
 * Hard requirements are per command: clang is required by the commands that
 * compile C code (build, b, compile, check, c, test, bench, lint, fix, run,
 * install), git by the commands that clone or fetch dependencies (fetch,
 * update, vendor, outdated, generate-lockfile, install-update, install).
 * A missing hard requirement prints an error and the function returns 1.
 * Soft requirements (make, curl, zstd, doxygen): prints a warning.
 *
 * When @p command_name is "help" or "version" the check is skipped entirely.
 *
 * @param command_name  The matched command name, or nullptr if no command was given.
 * @return 0 if all hard requirements are satisfied, 1 otherwise.
 */
[[nodiscard]]
i64 toolcheck_run(const char *command_name);

/**
 * @brief True when an executable named @p name is in a PATH directory.
 *
 * Used for the [build-dependencies] of a recipe, the host tools its
 * install.sh runs.  Names outside [A-Za-z0-9_-] are never found.
 */
bool toolcheck_on_path(const char *name);

#endif /* TOOLCHECK_H_ */
