#ifndef VERSION_H_
#define VERSION_H_

#include "coffee.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Version parsing and constraint checking.
 */

/* Parsed version triple (major.minor.patch, missing parts = 0) */
typedef struct {
	uint64_t major;
	uint64_t minor;
	uint64_t patch;
} version_t;

/*
 * Parse a version string into major.minor.patch.
 * Returns true on success, false on parse failure.
 */
bool version_parse(const char *s, version_t *out);

/*
 * Compare two versions: -1 if a < b, 0 if equal, 1 if a > b.
 */
int64_t version_cmp(const version_t *a, const version_t *b);

/*
 * Check if an actual version satisfies a version constraint.
 *
 * actual:     resolved version string, e.g. "1.2.3", "2.0", "1"   (must not be null)
 * constraint: version constraint, e.g. ">= 1.0.0", ">= 2.0, <= 2.4", "== 1.2.3", "*"
 *             If null, empty, or "*", the constraint is always satisfied.
 *
 * Supported operators: >=  <=  >  <  ==  !=
 * Multiple constraints separated by commas are AND-ed.
 * Whitespace around operators and commas is tolerated.
 * Missing version components default to 0 ("1" = 1.0.0, "1.2" = 1.2.0).
 *
 * Returns true if the constraint is satisfied, false otherwise.
 */
bool version_satisfies(const char *actual, const char *constraint);

#endif /* VERSION_H_ */
