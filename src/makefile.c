#include "makefile.h"

#include "build.h"
#include "lockfile.h"
#include "safe.h"
#include "safeio.h"
#include "strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Source list helpers                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
	sds   *items;
	size_t count;
	size_t capacity;
} src_list_t;

/* Append a source entry, deduplicating.  The linear scan makes this
 * O(n²) overall, but source lists are small (a handful of globs per
 * project) and the code stays simple. */
static void src_list_add(src_list_t *list, const char *src)
{
	if (src == nullptr || src[0] == '\0') {
		return;
	}
	for (size_t i = 0; i < list->count; i++) {
		if (strcmp(list->items[i], src) == 0) {
			return;
		}
	}
	if (list->count == list->capacity) {
		list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
		list->items    = safe_realloc(list->items, list->capacity * sizeof(sds));
	}
	list->items[list->count++] = sdsnew(src);
}

static int src_list_cmp(const void *a, const void *b)
{
	const sds *sa = (const sds *)a;
	const sds *sb = (const sds *)b;
	return strcmp(*sa, *sb);
}

static void src_list_free(src_list_t *list)
{
	for (size_t i = 0; i < list->count; i++) {
		sdsfree(list->items[i]);
	}
	safe_free(list->items);
	list->items    = nullptr;
	list->count    = 0;
	list->capacity = 0;
}

/* True when a source entry is a glob (contains a wildcard character). */
static bool is_glob(const char *src)
{
	return strchr(src, '*') != nullptr || strchr(src, '?') != nullptr;
}

/* Allowlist for single-word values interpolated into the generated
 * Makefile.  The file is parsed by make on every build (including
 * `coffee build clean`), so a third-party library.toml value or manifest
 * field must not be able to inject make or shell constructs.  Used for the
 * edition, the target directory and source globs, where a space is never
 * legitimate.  Rejects '$', backticks, ';', '#', whitespace, quotes and
 * every other metacharacter. */
static bool mk_value_is_safe(const char *s)
{
	if (s == nullptr || s[0] == '\0') {
		return false;
	}
	for (const char *p = s; *p != '\0'; p++) {
		char c = *p;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
		      c == '/' || c == '+' || c == '@' || c == '=' || c == '-' || c == '*' || c == '?')) {
			return false;
		}
	}
	return true;
}

/* Validation for path-class values: dependency flags (-I/-L/-l) and
 * non-glob dependency sources.  Unlike mk_value_is_safe() this allows a
 * space and bytes >= 0x80, because a dependency legitimately resolves
 * under $HOME, which may contain either.  Only the make and shell
 * metacharacters are refused, which is also what makes
 * mk_escape_spaces() sound: it can insert a backslash with no risk of
 * doubling up against one already in the value. */
static bool mk_token_is_safe(const char *s)
{
	if (s == nullptr || s[0] == '\0') {
		return false;
	}
	for (const char *p = s; *p != '\0'; p++) {
		char c = *p;
		if (c == '$' || c == '`' || c == ';' || c == '&' || c == '|' || c == '>' || c == '<' || c == '#' || c == '"' ||
		    c == '\'' || c == '\\' || c == '*' || c == '?' || c == '(' || c == ')' || c == '\n' || c == '\r' ||
		    c == '\t') {
			return false;
		}
	}
	return true;
}

/* Escape spaces so the value stays one make word.  Double quotes would
 * not do: make does not strip them in a prerequisite list, so
 * `SRC := "a b.c"` still parses as two targets.  A backslash works in
 * both positions — make reads `a\ b.c` as one word, and the shell
 * unescapes it before the compiler sees the argument.
 * Values without a space are emitted verbatim, which keeps the common
 * case free of noise.  Returns a new sds (caller frees).  Only call this
 * on a value mk_token_is_safe() accepted, which forbids '\\' itself. */
static sds mk_escape_spaces(const char *s)
{
	if (strchr(s, ' ') == nullptr) {
		return sdsnew(s);
	}
	sds out = sdsempty();
	for (const char *p = s; *p != '\0'; p++) {
		if (*p == ' ') {
			out = sdscatlen(out, "\\", 1);
		}
		out = sdscatlen(out, p, 1);
	}
	return out;
}

/* ------------------------------------------------------------------ */
/* Renderer                                                           */
/* ------------------------------------------------------------------ */

/* Resolve the output directory: the caller's value when it is a safe
 * single word, "build" otherwise.  Exported so the build path resolves the
 * executable against the same directory the Makefile will actually use,
 * otherwise a rejected --target-dir would build into build/ and then be
 * looked for under the rejected name. */
const char *makefile_resolve_target_dir(const char *target_dir)
{
	const char *tdir = target_dir != nullptr ? target_dir : "build";
	if (!mk_value_is_safe(tdir)) {
		return "build";
	}
	return tdir;
}

/* Append part to out and release it.  The render_* helpers each return a
 * fresh string the caller owns; keeping the append and the free in one
 * function stops a future edit from copying one and dropping the other,
 * which is how the fragments went unreleased before. */
static sds append_owned(sds out, sds part)
{
	out = sdscat(out, part);
	sdsfree(part);
	return out;
}

/* Variables block: compiler, flags, dependency flags and target paths. */
static sds render_variables(const manifest_t *m, const dep_graph_t *dg, resolved_features_t *rf, const char *target_dir,
                            bool release, bool debug, bool warn)
{
	sds out = sdsempty();

	/* Banner */
	out = sdscat(out, "# Generated by coffee — do not edit\n");

	/* CC: honour an explicit CC from the environment or the make command
	 * line, but beat make's built-in default (origin "default", value
	 * "cc") — the project needs clang for -std=c23.  A plain `?=` would
	 * not fire, because a built-in default is not "undefined". */
	out = sdscat(out, "ifeq ($(origin CC),default)\n");
	out = sdscat(out, "CC := clang\n");
	out = sdscat(out, "endif\n");

	/* CFLAGS: standard, optimization, debug symbols */
	const char *edition = m->package.edition != nullptr ? m->package.edition : "c23";
	if (!mk_value_is_safe(edition)) {
		if (warn) {
			fprintf_safe(stderr, "Warning: unsafe edition '%s', falling back to c23\n", edition);
		}
		edition = "c23";
	}
	out = sdscatprintf(out, "CFLAGS := -std=%s ", edition);
	if (debug) {
		out = sdscat(out, "-O0 -g -DDEBUG");
	} else if (release) {
		out = sdscat(out, "-O3");
	} else {
		out = sdscat(out, "-O3 -g");
	}
	out = sdscat(out, "\n");

	/* Project include directories */
	out = sdscatprintf(out, "CFLAGS += -Isrc -Iinclude/%s\n", m->package.name);

	/* Dependency flags: -I for every dep; -L only for deps that ship no
	 * sources (prebuilt libraries).  Deps with sources are compiled into
	 * the binary, so their -L/-l flags would double-link.  -l goes into
	 * LDLIBS, which the link rule expands after $(SOURCES): a static
	 * archive listed before the objects that need it fails ld's one-pass
	 * resolution. */
	sds ldlibs = sdsempty();
	if (dg != nullptr) {
		for (size_t i = 1; i < dep_graph_count(dg); i++) {
			const char *dep_name = dep_graph_node_name(dg, i);
			if (dep_name == nullptr) {
				continue;
			}
			size_t src_count = 0;
			(void)dep_graph_sources(dg, dep_name, &src_count);
			sds    flags  = dep_graph_flags(dg, dep_name);
			size_t ntok   = count_flag_tokens(flags);
			char **tokens = safe_malloc((ntok + 1) * sizeof(char *));
			size_t end    = 0;
			sds    copy   = split_flags_to_argv(flags, tokens, 0, &end);
			for (size_t t = 0; t < end; t++) {
				if (!mk_token_is_safe(tokens[t])) {
					if (warn) {
						fprintf_safe(stderr, "Warning: skipping unsafe flag '%s' from dependency '%s'\n", tokens[t],
						             dep_name);
					}
					continue;
				}
				sds quoted = mk_escape_spaces(tokens[t]);
				if (strncmp(tokens[t], "-I", 2) == 0 ||
				    (src_count == 0 && tokens[t][0] == '-' && strncmp(tokens[t], "-l", 2) != 0)) {
					out = sdscatprintf(out, "CFLAGS += %s\n", quoted);
				} else if (src_count == 0 && strncmp(tokens[t], "-l", 2) == 0) {
					ldlibs = sdscatprintf(ldlibs, " %s", quoted);
				}
				sdsfree(quoted);
			}
			sdsfree(copy);
			safe_free(tokens);
			sdsfree(flags);
		}
	}

	/* Feature defines */
	if (rf != nullptr) {
		size_t dflags_count = 0;
		sds   *dflags       = features_to_compiler_flags(rf, m->package.name, &dflags_count);
		if (dflags_count > 0) {
			out = sdscat(out, "CFLAGS +=");
			for (size_t i = 0; i < dflags_count; i++) {
				out = sdscatprintf(out, " %s", dflags[i]);
			}
			out = sdscat(out, "\n");
		}
		for (size_t i = 0; i < dflags_count; i++) {
			sdsfree(dflags[i]);
		}
		safe_free(dflags);
	}

	/* Static link */
	out = sdscat(out, "LDFLAGS := -static\n");

	/* Prebuilt-only deps link via -l, expanded after $(SOURCES) */
	out = sdscatprintf(out, "LDLIBS :=%s\n", ldlibs);
	sdsfree(ldlibs);

	/* Target */
	const char *tdir = makefile_resolve_target_dir(target_dir);
	if (tdir != target_dir && target_dir != nullptr && warn) {
		fprintf_safe(stderr, "Warning: unsafe target_dir '%s', falling back to build\n", target_dir);
	}
	out = sdscatprintf(out, "TARGET_DIR := %s\n", tdir);
	out = sdscatprintf(out, "TARGET := $(TARGET_DIR)/%s\n", m->package.name);

	return out;
}

/* The SOURCES line: project globs (or explicit files) plus dep sources. */
static sds render_sources(const manifest_t *m, const dep_graph_t *dg, bool warn)
{
	src_list_t sources = { 0 };
	if (m->package.sources_count > 0) {
		for (size_t i = 0; i < m->package.sources_count; i++) {
			src_list_add(&sources, m->package.sources[i]);
		}
	} else if (m->bin_count > 0 && m->bin[0].src_count > 0) {
		for (size_t i = 0; i < m->bin[0].src_count; i++) {
			src_list_add(&sources, m->bin[0].src[i]);
		}
	} else {
		src_list_add(&sources, "src/*.c");
	}
	if (dg != nullptr) {
		for (size_t i = 1; i < dep_graph_count(dg); i++) {
			const char *dep_name = dep_graph_node_name(dg, i);
			if (dep_name == nullptr) {
				continue;
			}
			size_t     src_count = 0;
			const sds *dep_srcs  = dep_graph_sources(dg, dep_name, &src_count);
			for (size_t s = 0; s < src_count; s++) {
				src_list_add(&sources, dep_srcs[s]);
			}
		}
	}
	if (sources.count > 0) {
		qsort(sources.items, sources.count, sizeof(sds), src_list_cmp);
	}

	/* A glob is passed to $(wildcard) and stays on the strict single-word
	 * predicate: GNU make's quote handling inside $(wildcard) is not
	 * something to depend on, so a glob containing a space is skipped.
	 * A concrete file path may legitimately live under a $HOME with a
	 * space in it, so it takes the path-class predicate and is quoted. */
	sds out = sdscat(sdsempty(), "SOURCES :=");
	for (size_t i = 0; i < sources.count; i++) {
		bool glob  = is_glob(sources.items[i]);
		bool valid = glob ? mk_value_is_safe(sources.items[i]) : mk_token_is_safe(sources.items[i]);
		if (!valid) {
			if (warn) {
				fprintf_safe(stderr, "Warning: skipping unsafe source '%s'\n", sources.items[i]);
			}
			continue;
		}
		if (glob) {
			out = sdscatprintf(out, " $(wildcard %s)", sources.items[i]);
			continue;
		}
		sds quoted = mk_escape_spaces(sources.items[i]);
		out        = sdscatprintf(out, " %s", quoted);
		sdsfree(quoted);
	}
	out = sdscat(out, "\n");

	src_list_free(&sources);
	return out;
}

/* Rules block: the .PHONY line and every recipe. */
static sds render_rules(const manifest_t *m)
{
	sds out = sdsempty();

	out = sdscat(out, "\n.PHONY: all clean test bench format tidy\n\n");
	out = sdscat(out, "all: $(TARGET)\n\n");
	out = sdscat(out, "$(TARGET): $(SOURCES)\n");
	out = sdscat(out, "\t@mkdir -p $(dir $(TARGET))\n");
	out = sdscat(out, "\t$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS) $(LDFLAGS)\n\n");
	out = sdscat(out, "clean:\n");
	out = sdscat(out, "\trm -rf $(TARGET_DIR)\n\n");
	out = sdscat(out, "test: all\n");
	out = sdscat(out, "\t@echo \"Run 'coffee test' to execute the test suite\"\n\n");
	out = sdscat(out, "bench: all\n");
	out = sdscat(out, "\t@echo \"No benchmarks defined yet; nothing to run\"\n\n");
	/* $(wildcard) so a project without a header directory is not an error */
	out = sdscatprintf(out, "format:\n\tclang-format -i $(wildcard src/*.c) $(wildcard include/%s/*.h)\n\n",
	                   m->package.name);
	out = sdscat(out, "tidy:\n\tclang-tidy src/*.c -- $(CFLAGS)\n");

	return out;
}

sds makefile_render(const manifest_t *m, const dep_graph_t *dg, resolved_features_t *rf, const char *target_dir,
                    bool release, bool debug, bool warn)
{
	/* The package name is interpolated into -I, the target path and the
	 * format target.  makefile_generate only ever passes a name that
	 * dep_name_is_valid() accepted, but check at the boundary rather than
	 * trusting a documented precondition. */
	if (m == nullptr || m->package.name == nullptr || !mk_value_is_safe(m->package.name)) {
		return nullptr;
	}

	sds out = sdsempty();

	out = append_owned(out, render_variables(m, dg, rf, target_dir, release, debug, warn));
	out = append_owned(out, render_sources(m, dg, warn));
	out = append_owned(out, render_rules(m));

	return out;
}

/* ------------------------------------------------------------------ */
/* Generation                                                         */
/* ------------------------------------------------------------------ */

/* Read a whole file into a new sds, or nullptr on failure. */
static sds read_file(const char *path)
{
	FILE *fp = safe_fopen(path, "r");
	if (fp == nullptr) {
		return nullptr;
	}
	sds    result = sdsempty();
	char   buf[4'096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		result = sdscatlen(result, buf, n);
	}
	safe_fclose(fp);
	return result;
}

i64 makefile_generate(manifest_t *m, const char *project_dir, resolved_features_t *rf, options *opts, bool warn)
{
	if (m == nullptr || project_dir == nullptr) {
		return 1;
	}

	/* Resolve the dependency graph (side-effect-free; no cache write) */
	lockfile_t *lock      = nullptr;
	sds         lock_path = sdscatprintf(sdsempty(), "%s/Coffee.lock", project_dir);
	if (safe_access(lock_path, F_OK) == 0) {
		lock = lockfile_parse(lock_path);
	}
	sdsfree(lock_path);

	dep_graph_t *dg = dep_graph_create(m, lock, opts->offline);
	lockfile_free(lock);
	if (dg == nullptr) {
		fprintf_safe(stderr, "Error: Could not resolve dependency graph\n");
		return 1;
	}

	sds content = makefile_render(m, dg, rf, opts->target_dir, opts->release, opts->debug, warn);
	dep_graph_free(dg);
	if (content == nullptr) {
		fprintf_safe(stderr, "Error: Could not render Makefile\n");
		return 1;
	}

	/* Compare with the existing file; write only when different */
	sds path = sdscatprintf(sdsempty(), "%s/Makefile", project_dir);
	sds old  = read_file(path);
	if (old != nullptr && strcmp(old, content) == 0) {
		sdsfree(old);
		sdsfree(path);
		sdsfree(content);
		return 0;
	}
	sdsfree(old);

	/* Write to a temp file, fsync, then rename over the target so a
	 * crash or ENOSPC mid-write cannot truncate a previously valid
	 * Makefile.  The temp name matches the other writers (manifest.c,
	 * lockfile.c, dep_graph_cache.c). */
	sds   tmp_path = tmp_path_for(path);
	FILE *fp       = safe_fopen_nofollow(tmp_path);
	if (fp == nullptr) {
		fprintf_safe(stderr, "Error: Could not write %s\n", tmp_path);
		sdsfree(tmp_path);
		sdsfree(path);
		sdsfree(content);
		return 1;
	}
	fprintf_safe(fp, "%s", content);
	if (fflush(fp) != 0) {
		safe_fclose(fp);
		remove(tmp_path);
		sdsfree(tmp_path);
		sdsfree(path);
		sdsfree(content);
		return 1;
	}
	if (fsync(fileno(fp)) != 0) {
		safe_fclose(fp);
		remove(tmp_path);
		sdsfree(tmp_path);
		sdsfree(path);
		sdsfree(content);
		return 1;
	}
	if (safe_fclose(fp) != 0) {
		remove(tmp_path);
		sdsfree(tmp_path);
		sdsfree(path);
		sdsfree(content);
		return 1;
	}
	if (safe_rename(tmp_path, path) != 0) {
		remove(tmp_path);
		sdsfree(tmp_path);
		sdsfree(path);
		sdsfree(content);
		return 1;
	}
	safe_fsync_dir(path);
	sdsfree(tmp_path);
	sdsfree(path);
	sdsfree(content);
	return 0;
}
