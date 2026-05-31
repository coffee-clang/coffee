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

#ifndef STRINGS_H_
#define STRINGS_H_

/**
 * @file strings.h
 * @brief Internal string-output utilities.
 *
 * This file provides convenience wrappers around the SDS string-building
 * library.  The functions herein are **internal to the coffee build
 * tool** and expose no public API surface.  They may be changed or
 * removed without notice.
 */

#include "../deps/sds/sds.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* glibc's inline bsearch (stdlib-bsearch.h) casts away const, triggering
 * -Wcast-qual on Clang because Clang's __GNUC_MINOR__ is too low for the
 * glibc-side pragma to activate.  Suppress the false positive here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#include <stdlib.h>
#pragma GCC diagnostic pop

/**
 * @brief Format text and write it to an arbitrary stream.
 *
 * Builds the output string with sdscatvprintf(3) and writes it
 * via fputs(3).  No `fprintf` / `vfprintf` family functions are
 * called, which keeps the codebase clear of banned functions.
 *
 * @param stream  The output stream (e.g. stdout, stderr, a file).
 * @param fmt     printf(3)-style format string.
 * @param ...     Arguments for the format string.
 *
 * @internal
 * This function is for internal use only and exposes no public
 * interface.
 */
static inline void fprintf_safe(FILE *stream, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sds str = sdscatvprintf(sdsempty(), fmt, ap);
	va_end(ap);
	if (fputs(str, stream) == EOF) {
		exit(1);
	}
	sdsfree(str);
}

/**
 * @brief Format text and write it to stdout.
 *
 * Convenience macro that calls fprintf_safe(stdout, ...).
 *
 * @param ...     printf(3)-style format string and arguments.
 *
 * @internal
 * This macro is for internal use only and exposes no public
 * interface.
 */
#define printf_safe(...) fprintf_safe(stdout, __VA_ARGS__)

/**
 * @brief Safely format text into a fixed-size buffer using SDS.
 *
 * Builds the output string with sdscatvprintf(3), then copies up to
 * size-1 characters into buf and NUL-terminates.  Behaviour mirrors
 * C11 snprintf(3): if buf is nullptr or size is 0, no data is written
 * but the length that *would* have been written is still returned.
 *
 * Uses sdscatvprintf(3) so all standard printf format specifiers are
 * supported (unlike sdscatfmt which only supports a subset).
 *
 * @param buf   Destination buffer (may be nullptr when size is 0).
 * @param size  Capacity of buf in bytes.
 * @param fmt   printf(3)-style format string.
 * @param ...   Arguments for the format string.
 * @return      The number of characters that would have been written
 *              (excluding the NUL terminator) had size been large
 *              enough.
 *
 * @internal
 * This function is for internal use only and exposes no public
 * interface.
 */
static inline int snprintf_safe(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sds str = sdscatvprintf(sdsempty(), fmt, ap);
	va_end(ap);

	int len = (int)sdslen(str);

	if (buf != nullptr && size > 0) {
		size_t copy = (size_t)len < size - 1 ? (size_t)len : size - 1;
		if (copy > 0) {
			memccpy(buf, str, '\0', copy);
		}
		buf[copy] = '\0';
	}

	sdsfree(str);
	return len;
}

#endif // STRINGS_H_
