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
	fputs(str, stream);
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

#endif // STRINGS_H_
