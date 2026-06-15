#ifndef TOOLCHECK_H_
#define TOOLCHECK_H_

#include <stdint.h>

typedef int64_t i64;

/**
 * @brief Check that required system tools exist on PATH.
 *
 * Hard requirements (clang, git): prints an error and the function returns 1.
 * Soft requirements (make, curl, zstd, doxygen): prints a warning.
 *
 * When @p command_name is "help" or "version" the check is skipped entirely.
 *
 * @param command_name  The matched command name, or nullptr if no command was given.
 * @return 0 if all hard requirements are satisfied, 1 otherwise.
 */
[[nodiscard]]
i64 toolcheck_run(const char *command_name);

#endif /* TOOLCHECK_H_ */
