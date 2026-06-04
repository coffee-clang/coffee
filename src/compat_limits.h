/* Prevent clang/glibc C23 *_WIDTH macro redefinition from <limits.h>.
 *
 * clang's built-in <limits.h> does #include_next <limits.h> to the glibc
 * version, which already defines the C23 *_WIDTH macros.  clang then defines
 * them again unconditionally, producing -Wmacro-redefined.
 *
 * This header pre-defines each *_WIDTH macro with clang's value (via its
 * built-in __*_WIDTH__ defines), guarded by #ifndef so that:
 *   - glibc sees the macros already defined and skips its definitions,
 *   - clang sees an identical redefinition (same token sequence) and
 *     does NOT produce -Wmacro-redefined.
 *
 * Include via -include, *after* all -D flags, *before* any #include. */

#ifndef COFFEE_COMPAT_LIMITS_H
#define COFFEE_COMPAT_LIMITS_H

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L

# ifndef BOOL_WIDTH
#  define BOOL_WIDTH __BOOL_WIDTH__
# endif
# ifndef CHAR_WIDTH
#  define CHAR_WIDTH CHAR_BIT
# endif
# ifndef SCHAR_WIDTH
#  define SCHAR_WIDTH CHAR_BIT
# endif
# ifndef UCHAR_WIDTH
#  define UCHAR_WIDTH CHAR_BIT
# endif
# ifndef USHRT_WIDTH
#  define USHRT_WIDTH __SHRT_WIDTH__
# endif
# ifndef SHRT_WIDTH
#  define SHRT_WIDTH __SHRT_WIDTH__
# endif
# ifndef UINT_WIDTH
#  define UINT_WIDTH __INT_WIDTH__
# endif
# ifndef INT_WIDTH
#  define INT_WIDTH __INT_WIDTH__
# endif
# ifndef ULONG_WIDTH
#  define ULONG_WIDTH __LONG_WIDTH__
# endif
# ifndef LONG_WIDTH
#  define LONG_WIDTH __LONG_WIDTH__
# endif
# ifndef ULLONG_WIDTH
#  define ULLONG_WIDTH __LLONG_WIDTH__
# endif
# ifndef LLONG_WIDTH
#  define LLONG_WIDTH __LLONG_WIDTH__
# endif

#endif /* __STDC_VERSION__ >= 202311L */

#endif /* COFFEE_COMPAT_LIMITS_H */
