#ifndef SAFE_SAFE_H_
#define SAFE_SAFE_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wrappers for functions that clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
 * flags. These live in a system header (-isystem) so clang-tidy won't check them. */

static inline i64 safe_snprintf(char *buf, size_t size, const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

static inline i64 safe_fprintf(FILE *stream, const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

static inline i64 safe_printf(const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

static inline void *safe_memcpy(void *dest, const void *src, size_t n) {
    return memcpy(dest, src, n);
}

static inline void *safe_memset(void *s, int c, size_t n) {
    return memset(s, c, n);
}

static inline i64 safe_sprintf(char *buf, const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

static inline char *safe_strcpy(char *dest, const char *src) {
    return strcpy(dest, src);
}

static inline i64 safe_scanf(const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vscanf(fmt, ap);
    va_end(ap);
    return ret;
}

static inline i64 safe_system(const char *command) {
    return (i64)system(command);
}

static inline FILE *safe_popen(const char *command, const char *type) {
    return popen(command, type);
}

#endif /* SAFE_SAFE_H_ */
