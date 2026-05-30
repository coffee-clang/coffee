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
#include <stdlib.h>
#include <string.h>

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
 * Builds the output string with sdscatfmt(3), then copies up to
 * size-1 characters into buf and NUL-terminates.  Behaviour mirrors
 * C11 snprintf(3): if buf is NULL or size is 0, no data is written
 * but the length that *would* have been written is still returned.
 *
 * This is a macro so that sdscatfmt(3) can receive the variadic
 * arguments directly (no va_list alternative exists for sdscatfmt).
 * The heavy lifting is delegated to the helper function
 * snprintf_copy_to_buf().
 *
 * @param buf   Destination buffer (may be NULL when size is 0).
 * @param size  Capacity of buf in bytes.
 * @param ...   sdscatfmt(3)-style format string and arguments.
 * @return      The number of characters that would have been written
 *              (excluding the NUL terminator) had size been large
 *              enough.
 *
 * @internal
 * This macro is for internal use only and exposes no public
 * interface.
 */
#define snprintf_safe(buf, size, ...) snprintf_copy_to_buf(buf, size, sdscatfmt(sdsempty(), __VA_ARGS__))

/**
 * @brief Copy an SDS string into a fixed-size buffer.
 *
 * Helper invoked by the snprintf_safe() macro.  Takes ownership of
 * the sds string and frees it.
 *
 * @param buf  Destination buffer.
 * @param size  Capacity of buf in bytes.
 * @param str   SDS string to copy from (will be freed).
 * @return      The length of str (before truncation).
 *
 * @internal
 * This function is for internal use only and exposes no public
 * interface.
 */
static inline int snprintf_copy_to_buf(char *buf, size_t size, sds str)
{
	int len = (int)sdslen(str);

	if (buf != nullptr && size > 0) {
		size_t copy = (size_t)len < size - 1 ? (size_t)len : size - 1;
		if (copy > 0) {
			memcpy(buf, str, copy);
		}
		buf[copy] = '\0';
	}

	sdsfree(str);
	return len;
}

#endif // STRINGS_H_
