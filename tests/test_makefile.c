#include "../src/coffee.h"
#include "../src/coffee_features.h"
#include "../src/dep_graph.h"
#include "../src/makefile.h"
#include "../src/manifest.h"
#include "../src/safeio.h"
#include "../src/strings.h"
#include "safe.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void coffee_register_makefile_tests(void);

TEST(makefile_is_created)
{
	const char *test_dir = "/tmp/coffee-makefile-project";
	mkdir(test_dir, 0755);

	sds make_path = sdscatprintf(sdsempty(), "%s/Makefile", test_dir);

	FILE *fp = fopen(make_path, "w");
	ASSERT(fp, "could not create test Makefile");

	fprintf_safe(fp, "CC ?= clang\n");
	fprintf_safe(fp, "CFLAGS += -std=c23 -O3 -g\n");
	fprintf_safe(fp, "CFLAGS += -Wall -Wextra -Wshadow -Wpedantic\n");
	fprintf_safe(fp, "CFLAGS += -Wconversion -Wsign-conversion -Wunused\n");
	fprintf_safe(fp, "CFLAGS += -Iinclude/test_project\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "TARGET := build/test_project\n");
	fprintf_safe(fp, "SOURCES := $(wildcard src/*.c)\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, ".PHONY: build clean format tidy\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "build: $(SOURCES)\n");
	fprintf_safe(fp, "\t$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "clean:\n");
	fprintf_safe(fp, "\trm -rf build/\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "format:\n");
	fprintf_safe(fp, "\tclang-format -i src/*.c include/test_project/*.h\n");
	fprintf_safe(fp, "\n");
	fprintf_safe(fp, "tidy:\n");
	fprintf_safe(fp, "\tclang-tidy src/*.c -- $(CFLAGS)\n");
	fclose(fp);

	fp = fopen(make_path, "r");
	ASSERT(fp, "Makefile should be readable");

	sds    buf = sdsempty();
	char   chunk[4'096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		buf = sdscatlen(buf, chunk, n);
	}
	fclose(fp);

	ASSERT(strstr(buf, "CC ?= clang") != nullptr, "Makefile missing CC");
	ASSERT(strstr(buf, "CFLAGS += -std=c23") != nullptr, "Makefile missing C standard");
	ASSERT(strstr(buf, "CFLAGS += -Iinclude/") != nullptr, "Makefile missing include path");
	ASSERT(strstr(buf, "build: $(SOURCES)") != nullptr, "Makefile missing build target");
	ASSERT(strstr(buf, "clean:") != nullptr, "Makefile missing clean target");
	ASSERT(strstr(buf, "format:") != nullptr, "Makefile missing format target");
	ASSERT(strstr(buf, "tidy:") != nullptr, "Makefile missing tidy target");

	sdsfree(buf);
	sdsfree(make_path);
	PASS();
}

TEST(dep_appended_to_makefile)
{
	const char *tmp_make = "/tmp/coffee-makefile-add-test.mk";
	FILE       *fp       = fopen(tmp_make, "w");
	ASSERT(fp, "could not create test Makefile");
	fprintf_safe(fp, "CC ?= clang\n");
	fprintf_safe(fp, "build:\n");
	fprintf_safe(fp, "\t$(CC) -o test src/*.c\n");
	fclose(fp);

	fp = fopen(tmp_make, "a");
	ASSERT(fp, "could not append to test Makefile");
	fprintf_safe(fp, "\n# Dep: mylib\n");
	fprintf_safe(fp, "CFLAGS += -Ideps/mylib/include\n");
	fprintf_safe(fp, "LDFLAGS += -Ldeps/mylib/lib -lmylib\n");
	fclose(fp);

	fp = fopen(tmp_make, "r");
	ASSERT(fp, "could not reopen Makefile");
	sds    buf = sdsempty();
	char   chunk[4'096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		buf = sdscatlen(buf, chunk, n);
	}
	fclose(fp);

	ASSERT(strstr(buf, "# Dep: mylib") != nullptr, "Makefile missing dep comment");
	ASSERT(strstr(buf, "-Ideps/mylib/include") != nullptr, "Makefile missing include path");
	ASSERT(strstr(buf, "-Ldeps/mylib/lib") != nullptr, "Makefile missing library path");
	ASSERT(strstr(buf, "-lmylib") != nullptr, "Makefile missing link flag");

	sdsfree(buf);
	remove(tmp_make);
	PASS();
}

TEST(build_finds_makefile)
{
	const char *tmp_dir = "/tmp/coffee-build-test";
	mkdir(tmp_dir, 0755);

	sds   make_path = sdscatprintf(sdsempty(), "%s/Makefile", tmp_dir);
	FILE *fp        = fopen(make_path, "w");
	ASSERT(fp, "could not create Makefile");
	fprintf_safe(fp, "CC ?= clang\n");
	fprintf_safe(fp, "build:\n");
	fprintf_safe(fp, "\t@echo built\n");
	fclose(fp);

	fp = fopen(make_path, "r");
	ASSERT(fp, "Makefile should be readable");
	fclose(fp);

	sdsfree(make_path);
	PASS();
}

TEST(add_skips_when_no_makefile)
{
	const char *fake_makefile = "/tmp/coffee-no-makefile-test/Makefile";
	remove(fake_makefile);

	FILE *fp = fopen(fake_makefile, "a");
	if (fp) {
		fclose(fp);
		FAIL("Makefile in non-existent directory should not be created");
		remove(fake_makefile);
		return 0;
	}

	PASS();
}

TEST(safe_package_name)
{
	const char *valid[] = { "mylib", "my_lib", "my-lib", "mylib123", "MyLib", nullptr };
	for (i64 i = 0; valid[i]; i++) {
		const char *p = valid[i];
		while (*p) {
			i64 c = (unsigned char)*p;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
				FAIL("valid name should pass validation");
				return 0;
			}
			p++;
		}
	}

	const char *invalid[] = { "bad name", "bad$name", "bad;name", "bad\nname", "$(shell)", nullptr };
	for (i64 i = 0; invalid[i]; i++) {
		const char *p     = invalid[i];
		i64         found = 0;
		while (*p && !found) {
			i64 c = (unsigned char)*p;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
				found = 1;
			}
			p++;
		}
		ASSERT(found, "invalid name should fail validation");
	}

	PASS();
}

/* ---------------------------------------------------------------
 * Generated Makefile
 * --------------------------------------------------------------- */

static manifest_t *make_test_manifest(const char *name, const char *edition)
{
	manifest_t *m      = safe_calloc(1, sizeof(manifest_t));
	m->package.name    = sdsnew(name);
	m->package.version = sdsnew("1.0.0");
	if (edition) {
		m->package.edition = sdsnew(edition);
	}
	return m;
}

TEST(render_basic)
{
	manifest_t *m   = make_test_manifest("hello", nullptr);
	sds         out = makefile_render(m, nullptr, nullptr, nullptr, false, false, true);
	ASSERT(out != nullptr, "render should succeed");
	ASSERT(strstr(out, "# Generated by coffee") != nullptr, "missing banner");
	ASSERT(strstr(out, "ifeq ($(origin CC),default)") != nullptr, "missing CC origin guard");
	ASSERT(strstr(out, "CC := clang") != nullptr, "missing CC");
	ASSERT(strstr(out, "-std=c23") != nullptr, "missing default standard");
	ASSERT(strstr(out, "-Isrc -Iinclude/hello") != nullptr, "missing project include");
	ASSERT(strstr(out, "LDFLAGS := -static") != nullptr, "missing static link");
	ASSERT(strstr(out, "LDLIBS :=") != nullptr, "missing LDLIBS line");
	ASSERT(strstr(out, "TARGET := $(TARGET_DIR)/hello") != nullptr, "missing target");
	ASSERT(strstr(out, "SOURCES := $(wildcard src/*.c)") != nullptr, "missing default sources");
	ASSERT(strstr(out, "all: $(TARGET)") != nullptr, "missing all target");
	ASSERT(strstr(out, "$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS) $(LDFLAGS)") != nullptr, "missing link rule");
	ASSERT(strstr(out, "clean:") != nullptr, "missing clean target");
	ASSERT(strstr(out, "test: all") != nullptr, "missing test target");
	ASSERT(strstr(out, "bench: all") != nullptr, "missing bench target");
	ASSERT(strstr(out, "format:") != nullptr, "missing format target");
	ASSERT(strstr(out, "tidy:") != nullptr, "missing tidy target");
	sdsfree(out);
	manifest_free(m);
	PASS();
}

TEST(render_edition_and_features)
{
	manifest_t *m       = make_test_manifest("feat", "c11");
	m->features_count   = 1;
	m->features         = safe_calloc(1, sizeof(feature_def_t));
	m->features[0].name = sdsnew("json");

	sds                  requested[] = { sdsnew("json") };
	resolved_features_t *rf          = features_resolve(m, requested, 1, false, false);
	sdsfree(requested[0]);
	ASSERT(rf != nullptr, "features should resolve");

	sds out = makefile_render(m, nullptr, rf, nullptr, false, false, true);
	ASSERT(strstr(out, "-std=c11") != nullptr, "missing edition flag");
	ASSERT(strstr(out, "-DFEATURE_JSON") != nullptr, "missing feature define");
	sdsfree(out);
	features_free(rf);
	manifest_free(m);
	PASS();
}

TEST(render_release_debug)
{
	manifest_t *m = make_test_manifest("rel", nullptr);

	sds out = makefile_render(m, nullptr, nullptr, nullptr, true, false, true);
	ASSERT(strstr(out, "-O3") != nullptr, "release should keep -O3");
	ASSERT(strstr(out, "-g") == nullptr, "release should drop -g");
	sdsfree(out);

	out = makefile_render(m, nullptr, nullptr, nullptr, false, true, true);
	ASSERT(strstr(out, "-O0 -g -DDEBUG") != nullptr, "debug flags");
	sdsfree(out);

	manifest_free(m);
	PASS();
}

TEST(render_dep_flags_and_sources)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	dep_graph_t *g         = safe_calloc(1, sizeof(dep_graph_t));
	g->count               = 2;
	g->capacity            = 2;
	g->nodes               = safe_calloc(2, sizeof(dep_node_t));
	g->nodes[0].name       = sdsnew("app");
	g->nodes[1].name       = sdsnew("mylib");
	g->nodes[1].flags      = sdsnew(" -I\"deps/mylib/include\" -L\"deps/mylib/lib\" -l\"mylib\"");
	g->nodes[1].sources    = safe_calloc(1, sizeof(sds));
	g->nodes[1].sources[0] = sdsnew("deps/mylib/src/mylib.c");
	g->nodes[1].src_count  = 1;

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "-Ideps/mylib/include") != nullptr, "missing dep include");
	ASSERT(strstr(out, "-Ldeps/mylib/lib") == nullptr, "dep with sources must not add -L");
	ASSERT(strstr(out, "-lmylib") == nullptr, "dep with sources must not add -l");
	ASSERT(strstr(out, "deps/mylib/src/mylib.c") != nullptr, "missing dep source");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_prebuilt_dep_link_flags)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	/* A prebuilt-only dep (no sources) must link via -l, expanded after
	 * the objects on the link line. */
	dep_graph_t *g    = safe_calloc(1, sizeof(dep_graph_t));
	g->count          = 2;
	g->capacity       = 2;
	g->nodes          = safe_calloc(2, sizeof(dep_node_t));
	g->nodes[0].name  = sdsnew("app");
	g->nodes[1].name  = sdsnew("mylib");
	g->nodes[1].flags = sdsnew(" -I\"deps/mylib/include\" -L\"deps/mylib/lib\" -l\"mylib\"");

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "-Ideps/mylib/include") != nullptr, "missing dep include");
	ASSERT(strstr(out, "-Ldeps/mylib/lib") != nullptr, "missing dep library path");
	ASSERT(strstr(out, "LDLIBS := -Ldeps/mylib/lib -lmylib") != nullptr, "-L/-l must go into LDLIBS");
	ASSERT(strstr(out, "$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS) $(LDFLAGS)") != nullptr, "missing link rule");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_skips_unsafe_dep_flag)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	/* A make construct in a dep flag must not reach the generated file. */
	dep_graph_t *g    = safe_calloc(1, sizeof(dep_graph_t));
	g->count          = 2;
	g->capacity       = 2;
	g->nodes          = safe_calloc(2, sizeof(dep_node_t));
	g->nodes[0].name  = sdsnew("app");
	g->nodes[1].name  = sdsnew("evil");
	g->nodes[1].flags = sdsnew(" -I\"deps/evil/include\" $(shell touch /tmp/pwned)");

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "$(shell") == nullptr, "make construct must be skipped");
	ASSERT(strstr(out, "pwned") == nullptr, "injected path must be skipped");
	ASSERT(strstr(out, "-Ideps/evil/include") != nullptr, "safe dep flag must survive");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

/* A minimal two-node graph (root + one dependency) for the render tests. */
static dep_graph_t *make_two_node_graph(const char *dep_name)
{
	dep_graph_t *g   = safe_calloc(1, sizeof(dep_graph_t));
	g->count         = 2;
	g->capacity      = 2;
	g->nodes         = safe_calloc(2, sizeof(dep_node_t));
	g->nodes[0].name = sdsnew("app");
	g->nodes[1].name = sdsnew(dep_name);
	return g;
}

TEST(render_keeps_dep_flags_with_space_in_path)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	/* A dependency may resolve under a $HOME containing a space; the
	 * token must survive, space-escaped so make keeps it one word. */
	dep_graph_t *g    = make_two_node_graph("mylib");
	g->nodes[1].flags = sdsnew(" -I\"/home/me/my deps/mylib/include\"");

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "CFLAGS += -I/home/me/my\\ deps/mylib/include") != nullptr,
	       "space in a dep path must be preserved and escaped");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_keeps_dep_source_with_space_in_path)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	dep_graph_t *g         = make_two_node_graph("mylib");
	g->nodes[1].sources    = safe_calloc(1, sizeof(sds));
	g->nodes[1].sources[0] = sdsnew("/home/me/my deps/mylib/src/mylib.c");
	g->nodes[1].src_count  = 1;

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "SOURCES := /home/me/my\\ deps/mylib/src/mylib.c") != nullptr,
	       "space in a dep source path must be preserved and escaped");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_keeps_non_ascii_dep_path)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	/* A $HOME containing non-ASCII bytes is a legitimate path. */
	dep_graph_t *g    = make_two_node_graph("mylib");
	g->nodes[1].flags = sdsnew(" -I\"/home/josé/.coffee/deps/mylib/include\"");

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "-I/home/josé/.coffee/deps/mylib/include") != nullptr, "non-ASCII path must survive");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_skips_shell_metachars_in_dep_flag)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	/* The path-class predicate allows spaces, so it must still refuse
	 * every shell and make metacharacter. */
	dep_graph_t *g    = make_two_node_graph("evil");
	g->nodes[1].flags = sdsnew(" -I\"deps/evil/include\" ;rm -rf / && echo x | tee y");

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, ";rm -rf /") == nullptr, "shell separator must be skipped");
	ASSERT(strstr(out, "&&") == nullptr, "shell conjunction must be skipped");
	ASSERT(strstr(out, "| tee") == nullptr, "pipe must be skipped");
	ASSERT(strstr(out, "-Ideps/evil/include") != nullptr, "safe dep flag must survive");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_skips_glob_with_metachar)
{
	manifest_t *m            = make_test_manifest("app", nullptr);
	m->package.sources       = safe_calloc(1, sizeof(sds));
	m->package.sources[0]    = sdsnew("src/$(shell).c");
	m->package.sources_count = 1;

	/* A glob goes to $(wildcard) and stays on the strict single-word
	 * predicate, so a make construct inside one is dropped. */
	sds out = makefile_render(m, nullptr, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "$(shell)") == nullptr, "make construct in a glob must be skipped");
	ASSERT(strstr(out, "SOURCES :=\n") != nullptr, "all sources were skipped");
	sdsfree(out);
	manifest_free(m);
	PASS();
}

TEST(render_rejects_invalid_package_name)
{
	manifest_t *m = make_test_manifest("a b", nullptr);

	sds out = makefile_render(m, nullptr, nullptr, nullptr, false, false, true);
	ASSERT(out == nullptr, "a package name that is not a safe single word must be rejected");
	manifest_free(m);
	PASS();
}

TEST(resolve_target_dir_passes_safe)
{
	ASSERT(strcmp(makefile_resolve_target_dir("mydir"), "mydir") == 0, "a safe target dir must pass through");
	ASSERT(strcmp(makefile_resolve_target_dir(nullptr), "build") == 0, "null must resolve to build");
	PASS();
}

TEST(resolve_target_dir_rejects_unsafe)
{
	ASSERT(strcmp(makefile_resolve_target_dir("build;rm -rf /"), "build") == 0,
	       "an unsafe target dir must resolve to build");
	ASSERT(strcmp(makefile_resolve_target_dir("my dir"), "build") == 0,
	       "a target dir with a space is not a single make word");
	PASS();
}

TEST(render_skips_unsafe_dep_source)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	dep_graph_t *g         = safe_calloc(1, sizeof(dep_graph_t));
	g->count               = 2;
	g->capacity            = 2;
	g->nodes               = safe_calloc(2, sizeof(dep_node_t));
	g->nodes[0].name       = sdsnew("app");
	g->nodes[1].name       = sdsnew("evil");
	g->nodes[1].sources    = safe_calloc(2, sizeof(sds));
	g->nodes[1].sources[0] = sdsnew("deps/evil/src/$(shell).c");
	g->nodes[1].sources[1] = sdsnew("deps/evil/src/evil.c");
	g->nodes[1].src_count  = 2;

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "$(shell)") == nullptr, "unsafe source must be skipped");
	ASSERT(strstr(out, "deps/evil/src/evil.c") != nullptr, "safe dep source must survive");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_falls_back_on_unsafe_edition)
{
	manifest_t *m = make_test_manifest("ed", "c23\n\t$(shell id)");

	sds out = makefile_render(m, nullptr, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "-std=c23") != nullptr, "unsafe edition must fall back to c23");
	ASSERT(strstr(out, "$(shell") == nullptr, "make construct must not reach the file");
	sdsfree(out);
	manifest_free(m);
	PASS();
}

TEST(render_falls_back_on_unsafe_target_dir)
{
	manifest_t *m = make_test_manifest("td", nullptr);

	sds out = makefile_render(m, nullptr, nullptr, "build;rm -rf /", false, false, true);
	ASSERT(strstr(out, "TARGET_DIR := build") != nullptr, "unsafe target_dir must fall back to build");
	ASSERT(strstr(out, "build;rm -rf /") == nullptr, "injection must not reach the file");
	sdsfree(out);
	manifest_free(m);
	PASS();
}

TEST(render_accepts_normal_values)
{
	manifest_t *m = make_test_manifest("app", nullptr);

	/* Regression guard: ordinary dep flags and sources must survive the
	 * allowlist unchanged. */
	dep_graph_t *g    = safe_calloc(1, sizeof(dep_graph_t));
	g->count          = 2;
	g->capacity       = 2;
	g->nodes          = safe_calloc(2, sizeof(dep_node_t));
	g->nodes[0].name  = sdsnew("app");
	g->nodes[1].name  = sdsnew("mylib");
	g->nodes[1].flags = sdsnew(" -I\"deps/mylib/include\" -L\"deps/mylib/lib\" -l\"mylib\"");

	sds out = makefile_render(m, g, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "-Ideps/mylib/include") != nullptr, "normal -I must survive");
	ASSERT(strstr(out, "-Ldeps/mylib/lib") != nullptr, "normal -L must survive");
	ASSERT(strstr(out, "-lmylib") != nullptr, "normal -l must survive");
	sdsfree(out);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_output_independent_of_warn)
{
	manifest_t *m      = make_test_manifest("app", nullptr);
	m->package.edition = sdsnew("c23\n\t$(shell id)");

	dep_graph_t *g    = make_two_node_graph("evil");
	g->nodes[1].flags = sdsnew(" -I\"deps/evil/include\" $(shell id)");

	/* warn only gates the diagnostics; the rendered text must not change,
	 * which is what makes `coffee build clean` safe to render silently. */
	sds loud  = makefile_render(m, g, nullptr, "build;rm -rf /", false, false, true);
	sds quiet = makefile_render(m, g, nullptr, "build;rm -rf /", false, false, false);
	ASSERT(loud != nullptr && quiet != nullptr, "render should succeed");
	ASSERT(strcmp(loud, quiet) == 0, "warn must not change the rendered Makefile");
	sdsfree(loud);
	sdsfree(quiet);
	dep_graph_free(g);
	manifest_free(m);
	PASS();
}

TEST(render_honors_cc_override)
{
	manifest_t *m = make_test_manifest("cc", nullptr);

	sds out = makefile_render(m, nullptr, nullptr, nullptr, false, false, true);
	ASSERT(strstr(out, "ifeq ($(origin CC),default)") != nullptr, "missing CC origin guard");
	ASSERT(strstr(out, "CC := clang") != nullptr, "missing clang default");
	ASSERT(strstr(out, "CC ?=") == nullptr, "CC ?= would not beat make's built-in default");
	sdsfree(out);
	manifest_free(m);
	PASS();
}

/* Pins the staging-name format: <path>.<pid>.tmp.  Per-process
 * uniqueness follows from the pid component, not from this assertion —
 * two calls in one process return the same name by construction. */
TEST(tmp_path_is_pid_suffixed)
{
	sds p = tmp_path_for("/tmp/Coffee.lock");

	char expect[64];
	snprintf_safe(expect, sizeof(expect), "/tmp/Coffee.lock.%d.tmp", (int)getpid());
	ASSERT(strcmp(p, expect) == 0, "temp path should be <path>.<pid>.tmp");
	sdsfree(p);

	/* The pid is what keeps two concurrent writers apart, so the staging
	 * name must differ from the old fixed "<path>.tmp" name. */
	sds fixed = sdscatprintf(sdsempty(), "%s.tmp", "/tmp/Coffee.lock");
	ASSERT(strcmp(fixed, expect) != 0, "staging name must differ from a plain .tmp name");
	sdsfree(fixed);
	PASS();
}

TEST(generate_leaves_no_temp_file)
{
	char tmpdir[] = "/tmp/coffee-mkgen2-XXXXXX";
	ASSERT(mkdtemp(tmpdir) != nullptr, "mkdtemp failed");

	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", tmpdir);
	FILE *fp        = fopen(toml_path, "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"genproj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse(toml_path);
	ASSERT(m != nullptr, "manifest should parse");

	options opt = { 0 };
	ASSERT(makefile_generate(m, tmpdir, nullptr, &opt, true) == 0, "generate should succeed");
	ASSERT(makefile_generate(m, tmpdir, nullptr, &opt, true) == 0, "second generate should succeed");

	/* Both runs must leave the directory clean: a failed rename would
	 * orphan the staging file. */
	DIR *d = opendir(tmpdir);
	ASSERT(d != nullptr, "opendir failed");
	struct dirent *ent;
	int            leftovers = 0;
	while ((ent = readdir(d)) != nullptr) {
		if (strstr(ent->d_name, ".tmp") != nullptr) {
			leftovers++;
		}
	}
	closedir(d);
	ASSERT(leftovers == 0, "no .tmp staging file may survive");

	sdsfree(toml_path);
	manifest_free(m);
	test_remove_tree(tmpdir);
	PASS();
}

/* A symlink planted at the staging path must not be followed: fopen(..., "w")
 * would truncate the link target, so the writer refuses the open instead. */
TEST(generate_refuses_symlinked_staging)
{
	char tmpdir[] = "/tmp/coffee-mkgen3-XXXXXX";
	ASSERT(mkdtemp(tmpdir) != nullptr, "mkdtemp failed");

	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", tmpdir);
	FILE *fp        = fopen(toml_path, "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"genproj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse(toml_path);
	ASSERT(m != nullptr, "manifest should parse");

	/* Victim file whose content must survive the attack. */
	sds victim_path = sdscatprintf(sdsempty(), "%s/victim.txt", tmpdir);
	fp              = fopen(victim_path, "w");
	ASSERT(fp != nullptr, "could not write victim");
	fprintf_safe(fp, "precious\n");
	fclose(fp);

	/* Plant a symlink at the staging path, pointing at the victim. */
	sds make_path = sdscatprintf(sdsempty(), "%s/Makefile", tmpdir);
	sds staging   = tmp_path_for(make_path);
	ASSERT(symlink(victim_path, staging) == 0, "symlink should be planted");

	options opt = { 0 };
	i64     ret = makefile_generate(m, tmpdir, nullptr, &opt, true);

	/* Capture the post-attack state, then clean up before asserting so
	 * a failure cannot leak the tmpdir and the staging symlink. */
	char buf[64]           = { 0 };
	int  victim_exists     = access(victim_path, F_OK) == 0;
	int  victim_content_ok = test_read_first_line(victim_path, buf, sizeof(buf));
	int  no_makefile       = access(make_path, F_OK) != 0;

	unlink(staging);
	sdsfree(staging);
	sdsfree(make_path);
	sdsfree(victim_path);
	sdsfree(toml_path);
	manifest_free(m);
	test_remove_tree(tmpdir);

	ASSERT(ret != 0, "generate must refuse a symlinked staging file");
	ASSERT(victim_exists, "victim should still exist");
	ASSERT(victim_content_ok, "victim should be non-empty");
	ASSERT(strcmp(buf, "precious\n") == 0, "victim content must be unchanged");
	ASSERT(no_makefile, "no Makefile may be produced");
	PASS();
}

TEST(generate_creates_and_preserves)
{
	char tmpdir[] = "/tmp/coffee-mkgen-XXXXXX";
	ASSERT(mkdtemp(tmpdir) != nullptr, "mkdtemp failed");

	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", tmpdir);
	FILE *fp        = fopen(toml_path, "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"genproj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);

	manifest_t *m = manifest_parse(toml_path);
	ASSERT(m != nullptr, "manifest should parse");

	options opt = { 0 };
	i64     ret = makefile_generate(m, tmpdir, nullptr, &opt, true);
	ASSERT(ret == 0, "generate should succeed");

	sds make_path = sdscatprintf(sdsempty(), "%s/Makefile", tmpdir);
	ASSERT(access(make_path, F_OK) == 0, "Makefile should exist");

	/* A second call with unchanged inputs must not rewrite the file.
	 * Backdate the Makefile to a fixed old timestamp so the mtime
	 * comparison is deterministic without sleeping. */
	struct timespec backdate[2] = {
		{ .tv_sec = 1'000'000'000, .tv_nsec = 0 },
		{ .tv_sec = 1'000'000'000, .tv_nsec = 0 },
	};
	ASSERT(utimensat(AT_FDCWD, make_path, backdate, 0) == 0, "utimensat failed");
	struct stat st1;
	struct stat st2;
	ASSERT(stat(make_path, &st1) == 0, "stat failed");
	ret = makefile_generate(m, tmpdir, nullptr, &opt, true);
	ASSERT(ret == 0, "second generate should succeed");
	ASSERT(stat(make_path, &st2) == 0, "stat failed");
	ASSERT(st1.st_mtime == st2.st_mtime, "Makefile must not be rewritten when unchanged");

	/* Changing the manifest must rewrite the file */
	fp = fopen(toml_path, "w");
	ASSERT(fp != nullptr, "could not rewrite Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"genproj\"\nversion = \"1.0.0\"\nedition = \"c11\"\n");
	fclose(fp);
	manifest_free(m);
	m = manifest_parse(toml_path);
	ASSERT(m != nullptr, "manifest should reparse");
	ret = makefile_generate(m, tmpdir, nullptr, &opt, true);
	ASSERT(ret == 0, "third generate should succeed");
	ASSERT(stat(make_path, &st2) == 0, "stat failed");
	ASSERT(st1.st_mtime != st2.st_mtime, "Makefile must be rewritten when content changes");

	sdsfree(make_path);
	sdsfree(toml_path);
	manifest_free(m);
	test_remove_tree(tmpdir);
	PASS();
}

/* A Makefile without the coffee banner is hand-written: generation must
 * refuse to replace it and leave it untouched. */
TEST(generate_refuses_handwritten_makefile)
{
	char tmpdir[] = "/tmp/coffee-mkgen4-XXXXXX";
	ASSERT(mkdtemp(tmpdir) != nullptr, "mkdtemp failed");

	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", tmpdir);
	sds   make_path = sdscatprintf(sdsempty(), "%s/Makefile", tmpdir);
	FILE *fp        = fopen(toml_path, "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"genproj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n");
	fclose(fp);
	fp = fopen(make_path, "w");
	ASSERT(fp != nullptr, "could not write Makefile");
	fprintf_safe(fp, "all:\n\techo mine\n");
	fclose(fp);

	manifest_t *m = manifest_parse(toml_path);
	ASSERT(m != nullptr, "manifest should parse");

	options opt     = { 0 };
	i64     ret     = makefile_generate(m, tmpdir, nullptr, &opt, false);
	char    buf[64] = { 0 };
	int     read_ok = test_read_first_line(make_path, buf, sizeof(buf));

	/* A Makefile that starts with the banner is coffee's and is replaced. */
	fp = fopen(make_path, "w");
	ASSERT(fp != nullptr, "could not rewrite Makefile");
	fprintf_safe(fp, "# Generated by coffee — do not edit\nall:\n");
	fclose(fp);
	i64  ret2      = makefile_generate(m, tmpdir, nullptr, &opt, false);
	char buf2[64]  = { 0 };
	int  read2_ok  = test_read_first_line(make_path, buf2, sizeof(buf2));
	sds  generated = sdsnew(buf2);

	sdsfree(make_path);
	sdsfree(toml_path);
	manifest_free(m);
	test_remove_tree(tmpdir);

	ASSERT(ret != 0, "a hand-written Makefile must not be overwritten");
	ASSERT(read_ok && strcmp(buf, "all:\n") == 0, "the hand-written Makefile is unchanged");
	ASSERT(ret2 == 0, "a generated Makefile is replaced");
	ASSERT(read2_ok && strstr(generated, "# Generated by coffee") != nullptr, "banner written");
	sdsfree(generated);
	PASS();
}

/* [compilation] static = false drops -static from the link, and survives a
 * manifest round trip. */
TEST(render_static_false)
{
	manifest_t *m               = make_test_manifest("dyn", nullptr);
	m->compilation.dynamic_link = true;
	sds out                     = makefile_render(m, nullptr, nullptr, nullptr, false, false, true);
	ASSERT(out != nullptr, "render should succeed");
	ASSERT(strstr(out, "-static") == nullptr, "no -static with static = false");
	ASSERT(strstr(out, "LDFLAGS :=\n") != nullptr, "empty LDFLAGS");
	sdsfree(out);
	manifest_free(m);

	char tmpdir[] = "/tmp/coffee-mkstatic-XXXXXX";
	ASSERT(mkdtemp(tmpdir) != nullptr, "mkdtemp failed");
	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", tmpdir);
	FILE *fp        = fopen(toml_path, "w");
	ASSERT(fp != nullptr, "could not write Coffee.toml");
	fprintf_safe(fp, "[package]\nname = \"dyn\"\nversion = \"1.0.0\"\n\n[compilation]\nstatic = false\n");
	fclose(fp);
	m            = manifest_parse(toml_path);
	bool parsed  = m != nullptr && m->compilation.dynamic_link;
	i64  written = m != nullptr ? manifest_write(toml_path, m) : 1;
	manifest_free(m);
	m             = manifest_parse(toml_path);
	bool reparsed = m != nullptr && m->compilation.dynamic_link;
	manifest_free(m);
	sdsfree(toml_path);
	test_remove_tree(tmpdir);

	ASSERT(parsed, "static = false parsed");
	ASSERT(written == 0, "manifest written");
	ASSERT(reparsed, "static = false survives manifest_write");
	PASS();
}

/* The generated Makefile and coffee test share manifest_project_sources(). */
TEST(project_sources_resolution)
{
	manifest_t *m     = make_test_manifest("srcs", nullptr);
	size_t      count = 0;
	sds        *src   = manifest_project_sources(m, &count);
	ASSERT(count == 1 && strcmp(src[0], "src/*.c") == 0, "default glob");
	sdsfree(src[0]);
	safe_free(src);

	m->bin_count        = 1;
	m->bin              = safe_calloc(1, sizeof(binary_target_t));
	m->bin[0].name      = sdsnew("tool");
	m->bin[0].src_count = 1;
	m->bin[0].src       = safe_calloc(1, sizeof(sds));
	m->bin[0].src[0]    = sdsnew("src/tool.c");
	src                 = manifest_project_sources(m, &count);
	ASSERT(count == 1 && strcmp(src[0], "src/tool.c") == 0, "first [[bin]] src");
	sdsfree(src[0]);
	safe_free(src);

	m->package.sources_count = 2;
	m->package.sources       = safe_calloc(2, sizeof(sds));
	m->package.sources[0]    = sdsnew("lib/*.c");
	m->package.sources[1]    = sdsnew("main.c");
	src                      = manifest_project_sources(m, &count);
	ASSERT(count == 2 && strcmp(src[0], "lib/*.c") == 0 && strcmp(src[1], "main.c") == 0, "[package] sources win");
	sdsfree(src[0]);
	sdsfree(src[1]);
	safe_free(src);
	manifest_free(m);
	PASS();
}

void coffee_register_makefile_tests(void)
{
	TEST_REGISTER(makefile_is_created);
	TEST_REGISTER(generate_refuses_handwritten_makefile);
	TEST_REGISTER(render_static_false);
	TEST_REGISTER(project_sources_resolution);
	TEST_REGISTER(dep_appended_to_makefile);
	TEST_REGISTER(build_finds_makefile);
	TEST_REGISTER(add_skips_when_no_makefile);
	TEST_REGISTER(safe_package_name);
	TEST_REGISTER(render_basic);
	TEST_REGISTER(render_edition_and_features);
	TEST_REGISTER(render_release_debug);
	TEST_REGISTER(render_dep_flags_and_sources);
	TEST_REGISTER(render_prebuilt_dep_link_flags);
	TEST_REGISTER(render_keeps_dep_flags_with_space_in_path);
	TEST_REGISTER(render_keeps_dep_source_with_space_in_path);
	TEST_REGISTER(render_keeps_non_ascii_dep_path);
	TEST_REGISTER(render_skips_shell_metachars_in_dep_flag);
	TEST_REGISTER(render_skips_glob_with_metachar);
	TEST_REGISTER(render_rejects_invalid_package_name);
	TEST_REGISTER(render_output_independent_of_warn);
	TEST_REGISTER(resolve_target_dir_passes_safe);
	TEST_REGISTER(resolve_target_dir_rejects_unsafe);
	TEST_REGISTER(render_skips_unsafe_dep_flag);
	TEST_REGISTER(render_skips_unsafe_dep_source);
	TEST_REGISTER(render_falls_back_on_unsafe_edition);
	TEST_REGISTER(render_falls_back_on_unsafe_target_dir);
	TEST_REGISTER(render_accepts_normal_values);
	TEST_REGISTER(render_honors_cc_override);
	TEST_REGISTER(tmp_path_is_pid_suffixed);
	TEST_REGISTER(generate_leaves_no_temp_file);
	TEST_REGISTER(generate_refuses_symlinked_staging);
	TEST_REGISTER(generate_creates_and_preserves);
}
