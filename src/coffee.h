#ifndef COFFEE_H_
#define COFFEE_H_

/* sds.h is vendored and not lint-clean; suppress its warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-int-conversion"
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#include <sds/sds.h>
#include <sds/sdsalloc.h>
#pragma GCC diagnostic pop
#include "cmdline.h"
#include "strings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t       u8;
// typedef char16_t  c16;
typedef int32_t       b32;
typedef int32_t       i32;
typedef uint32_t      u32;
typedef uint64_t      u64;
typedef int64_t       i64;
typedef float         f32;
typedef double        f64;
typedef uintptr_t     uptr;
typedef unsigned char byte;
typedef ptrdiff_t     size;
typedef size_t        usize;

#define countof(a)   (size)(sizeof(a) / sizeof(*(a)))
#define lengthof(s)  (countof(s) - 1)
#define new(a, t, n) (t *)alloc(a, sizeof(t), _Alignof(t), n)

typedef struct options_s {
	// Pointer fields
	sds  error_code;
	sds *inputs;
	sds  pkg_version;
	sds  toolchain;
	sds  path;
	sds  git;
	sds  branch;
	sds  tag;
	sds  rev;
	sds  registry;
	sds  bin;
	sds  example;
	sds  features;
	sds  profile;
	sds  target;
	sds  target_dir;
	sds  manifest_path;

	// Integer fields
	i64 color;
	i64 inputs_num;
	i64 jobs;

	// Boolean fields
	bool verbose;
	bool verbose2;
	bool quiet;
	bool locked;
	bool offline;
	bool dev;
	bool build_dep;
	bool optional;
	bool release;
	bool debug;
	bool all_features;
	bool no_default_features;
	bool lib;
	bool fix;
} options_s, options;

typedef struct {
	sds name;
	sds description;
	i64 (*action)(struct options_s *);
} command_s;

extern command_s commands[];

/*
 * List of all available commands
 */

extern i64 handle_add(options *);
extern i64 handle_build(options *);
extern i64 handle_cflags(options *);
extern i64 handle_bench(options *);
extern i64 handle_check(options *);
extern i64 handle_clean(options *);
extern i64 handle_compile(options *);
extern i64 handle_config(options *);
extern i64 handle_doc(options *);
extern i64 handle_libs(options *);
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
extern i64 handle_list(options *);
extern i64 handle_locate_project(options *);
extern i64 handle_logout(options *);
extern i64 handle_machete(options *);
extern i64 handle_metadata(options *);
extern i64 handle_new(options *);
extern i64 handle_outdated(options *);
extern i64 handle_package(options *);
extern i64 handle_pkgid(options *);
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

#endif // COFFEE_H_
