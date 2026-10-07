// SPDX-License-Identifier: MIT
//
// Copyright (c) 2025 coffee-clang
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef SAFEIO_H_
#define SAFEIO_H_

/**
 * @file safeio.h
 * @brief Staging-file I/O for the write-then-rename writers.
 *
 * Every writer that renames over its target (Coffee.toml, Coffee.lock,
 * the generated Makefile and the dep-graph cache) stages through a temp
 * file.  This header owns the helpers those writers share: the
 * per-process staging path and the no-symlink open.
 *
 * These wrappers live here rather than in include/safe.h because safe.h
 * is vendored and must not be edited in-tree; a future reader should
 * not "fix" them into safe.h and break `make bootstrap`.
 */

/* sds.h is vendored and not lint-clean; suppress its warnings.  This
 * block is duplicated from strings.h on purpose: safeio.h must not
 * depend on strings.h. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-int-conversion"
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#include <sds/sds.h>
#pragma GCC diagnostic pop

#include "safe.h"

#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>

/**
 * @brief Wrap fdopen(3), which is on the banned-function list.
 *
 * Same naming convention as safe_fopen/safe_open/safe_close in
 * include/safe.h: the ban targets direct calls, and the safe_* name
 * marks this as the sanctioned home.  Project-owned safe_* wrappers
 * are the sanctioned exception to the ban; there is no way to obtain
 * a FILE * from an fd without fdopen(3).
 */
static inline FILE *safe_fdopen(int fd, const char *mode)
{
	return fdopen(fd, mode);
}

/**
 * @brief Open a staging file for writing, refusing to follow symlinks.
 *
 * The staging file is always one we are creating, never a pre-existing
 * target, so O_NOFOLLOW cannot break a legitimate case: it only makes
 * open(2) fail with ELOOP when an attacker has pre-created the staging
 * path as a symlink.  Without it, fopen(..., "w") would follow the
 * symlink and truncate a file of the attacker's choosing.
 *
 * O_NOFOLLOW does not block hard links, but they are out of scope:
 * with fs.protected_hardlinks=1 (the Linux default) an attacker can
 * only hard-link files they already own or can write, so there is no
 * privilege escalation.
 *
 * Returns a FILE * or nullptr on failure (the fd is closed and the
 * empty staging file removed if fdopen fails).
 */
static inline FILE *safe_fopen_nofollow(const char *path)
{
	int fd = safe_open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
	if (fd < 0) {
		return nullptr;
	}
	FILE *fp = safe_fdopen(fd, "w");
	if (fp == nullptr) {
		safe_close(fd);
		remove(path);
	}
	return fp;
}

/**
 * @brief Build the temp-file path used by the write-then-rename writers.
 *
 * The four writers that rename over their target share this helper:
 * manifest.c (Coffee.toml), lockfile.c (Coffee.lock), makefile.c (the
 * generated Makefile) and dep_graph_cache.c (the dep-graph cache).
 *
 * A fixed "<path>.tmp" name lets two concurrent writers in one project
 * share the same staging file, so one process can rename another
 * process's half-written content into place.  The pid component makes
 * the name unique per process; the ".tmp" suffix keeps the staged file
 * recognisable.
 *
 * Returns a new sds (caller frees).
 */
static inline sds tmp_path_for(const char *path)
{
	return sdscatprintf(sdsempty(), "%s.%d.tmp", path, (int)getpid());
}

#endif // SAFEIO_H_
