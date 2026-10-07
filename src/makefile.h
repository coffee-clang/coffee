#ifndef MAKEFILE_H_
#define MAKEFILE_H_

#include "coffee.h"
#include "coffee_features.h"
#include "dep_graph.h"
#include "manifest.h"

/*
 * Resolve the output directory used by both the generated Makefile and
 * the executable path `coffee run` resolves: the caller's value when it is
 * a safe single word, "build" otherwise.  Returns a string literal or the
 * borrowed pointer passed in — never freed by the caller.
 */
const char *makefile_resolve_target_dir(const char *target_dir);

/*
 * Render the generated Makefile for a project.
 *
 * Pure function: no I/O, no globals.  The output is a self-contained,
 * static build: project sources plus every dependency's sources are
 * compiled into a single static binary, with -I and -DFEATURE_* flags
 * baked in.
 *
 * m->package.name must be a safe single word; anything else returns
 * nullptr.  Every other interpolated value is validated inside the
 * renderer: single-word values (edition, target_dir, source globs) must
 * match mk_value_is_safe(), path-class values (dependency flags and
 * non-glob dependency sources) must match mk_token_is_safe() and are
 * space-escaped so they stay one make word.  Unsafe values are skipped
 * with a warning, or fall back to a safe default.
 *
 * Returns a new sds (caller frees) or nullptr on error.
 *
 * warn gates the stderr diagnostics only: a rejected value is skipped or
 * replaced either way, so the returned text does not depend on it.  The
 * build passes false for the `clean` target, which re-renders but uses
 * none of the values a warning would be about.
 */
sds makefile_render(const manifest_t *m, const dep_graph_t *dg, resolved_features_t *rf, const char *target_dir,
                    bool release, bool debug, bool warn);

/*
 * Generate the Makefile for a project at <project_dir>/Makefile.
 *
 * Resolves the dependency graph and features, renders the Makefile,
 * and writes it only when the content differs from the existing file
 * (so an up-to-date build has no effect on disk).  Returns 0 on
 * success, including the no-change case.
 */
i64 makefile_generate(manifest_t *m, const char *project_dir, resolved_features_t *rf, options *opts, bool warn);

#endif /* MAKEFILE_H_ */
