#ifndef COFFEE_H_
#define COFFEE_H_

#define _GNU_SOURCE
#include "../deps/sds/sds.h"
#include "../deps/sds/sdsalloc.h"
#include "cmdline.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t	  u8;
// typedef char16_t  c16;
typedef int32_t	  b32;
typedef int32_t	  i32;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int64_t	  i64;
typedef float	  f32;
typedef double	  f64;
typedef uintptr_t uptr;
typedef char	  byte;
typedef ptrdiff_t size;
typedef size_t	  usize;

#define countof(a)   (size)(sizeof(a) / sizeof(*(a)))
#define lengthof(s)  (countof(s) - 1)
#define new(a, t, n) (t *)alloc(a, sizeof(t), _Alignof(t), n)

typedef struct options_s {
	bool   verbose;
	bool   verbose2;
	bool   quiet;
	i64    color;
	bool   locked;
	bool   offline;
	char  *error_code;
	char **inputs;
	int    inputs_num;
} options_s, options;

typedef struct {
	char *name;
	char *description;
	i64 (*action)(struct options_s *);
} command_s;

/*
 * List of all available commands
 */

extern i64 handle_add(options *);
extern i64 handle_build(options *);
extern i64 handle_bench(options *);
extern i64 handle_check(options *);
extern i64 handle_clean(options *);
extern i64 handle_compile(options *);
extern i64 handle_config(options *);
extern i64 handle_doc(options *);
extern i64 handle_lint(options *);
extern i64 handle_fetch(options *);
extern i64 handle_fix(options *);
extern i64 handle_fmt(options *);
extern i64 handle_generate_lockfile(options *);
extern i64 handle_grep(options *);
extern i64 handle_help(options *);
extern i64 handle_info(options *);
extern i64 handle_init(options *);
extern i64 handle_install(options *);
extern i64 handle_install_update(options *);
extern i64 handle_install_update_config(options *);
extern i64 handle_locate_project(options *);
extern i64 handle_login(options *);
extern i64 handle_logout(options *);
extern i64 handle_machete(options *);
extern i64 handle_metadata(options *);
extern i64 handle_miri(options *);
extern i64 handle_new(options *);
extern i64 handle_owner(options *);
extern i64 handle_package(options *);
extern i64 handle_pkgid(options *);
extern i64 handle_publish(options *);
extern i64 handle_remove(options *);
extern i64 handle_report(options *);
extern i64 handle_rm(options *);
extern i64 handle_run(options *);
extern i64 handle_search(options *);
extern i64 handle_test(options *);
extern i64 handle_tree(options *);
extern i64 handle_uninstall(options *);
extern i64 handle_update(options *);
extern i64 handle_vendor(options *);
extern i64 handle_version(options *);
extern i64 handle_yank(options *);

#endif // COFFEE_H_
