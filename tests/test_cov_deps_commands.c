/*
 * Additional unit tests that raise per-function coverage (deps_commands).
 *
 * Covers the dependency graph (dep_graph.c, dep_graph_cache.c,
 * dep_graph_git.c) and the dependency related commands: add, fetch,
 * generate-lockfile, install, install-update, install-update-config,
 * outdated, remove, uninstall, update and vendor.
 *
 * Every test works inside a private mkdtemp() directory, with COFFEE_HOME
 * and COFFEE_REGISTRY_URL pointing into it, so no test touches the real
 * ~/.coffee or the network.  Git sources are local repositories.
 */

#include "../src/build.h"
#include "../src/coffee.h"
#include "../src/dep_graph.h"
#include "../src/lockfile.h"
#include "../src/manifest.h"
#include "../src/project.h"
#include "../src/strings.h"
#include "safe.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <toml.h>
#include <unistd.h>

void coffee_register_cov_deps_commands_tests(void);

/* ------------------------------------------------------------------ */
/* Fixture helpers                                                      */
/* ------------------------------------------------------------------ */

static char saved_cwd[4'096];
static sds  saved_home;
static sds  saved_registry;
static sds  saved_path;
static sds  saved_cc;
static int  saved_fds[3] = { -1, -1, -1 };

/* Create a unique scratch directory under /tmp. */
static sds tmp_new(void)
{
	sds path = sdsnew("/tmp/coffee-covdeps-XXXXXX");
	if (mkdtemp(path) == nullptr) {
		sdsfree(path);
		return nullptr;
	}
	return path;
}

/* Join two path components into a new sds. */
static sds join(const char *dir, const char *rel)
{
	return sdscatprintf(sdsempty(), "%s/%s", dir, rel);
}

static void write_text(const char *path, const char *text)
{
	FILE *fp = fopen(path, "w");
	if (fp == nullptr) {
		return;
	}
	fprintf_safe(fp, "%s", text);
	fclose(fp);
}

static sds read_text(const char *path)
{
	sds   out = sdsempty();
	FILE *fp  = fopen(path, "r");
	if (fp == nullptr) {
		return out;
	}
	char   buf[4'096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		out = sdscatlen(out, buf, n);
	}
	fclose(fp);
	return out;
}

static bool file_contains(const char *path, const char *needle)
{
	sds  text  = read_text(path);
	bool found = strstr(text, needle) != nullptr;
	sdsfree(text);
	return found;
}

static void enter_dir(const char *dir)
{
	if (getcwd(saved_cwd, sizeof(saved_cwd)) == nullptr) {
		saved_cwd[0] = '/';
		saved_cwd[1] = '\0';
	}
	if (chdir(dir) != 0) {
		saved_cwd[0] = '\0';
	}
}

/* Leave the scratch directory and delete it. */
static void leave_dir(sds root)
{
	if (chdir(saved_cwd) != 0) {
		return;
	}
	test_remove_tree(root);
	sdsfree(root);
}

static sds env_save(const char *name)
{
	const char *value = getenv(name);
	return value != nullptr ? sdsnew(value) : nullptr;
}

static void env_restore(const char *name, sds saved)
{
	if (saved != nullptr) {
		setenv(name, saved, 1);
		sdsfree(saved);
	} else {
		unsetenv(name);
	}
}

/* Point COFFEE_HOME and COFFEE_REGISTRY_URL at fresh dirs inside root. */
static void sandbox_on(const char *root)
{
	saved_home     = env_save("COFFEE_HOME");
	saved_registry = env_save("COFFEE_REGISTRY_URL");
	sds home       = join(root, "home");
	sds registry   = join(root, "registry");
	mkdir(home, 0755);
	mkdir(registry, 0755);
	setenv("COFFEE_HOME", home, 1);
	sds url = sdscatprintf(sdsempty(), "file://%s", registry);
	setenv("COFFEE_REGISTRY_URL", url, 1);
	sdsfree(url);
	sdsfree(registry);
	sdsfree(home);
}

static void sandbox_off(void)
{
	env_restore("COFFEE_HOME", saved_home);
	env_restore("COFFEE_REGISTRY_URL", saved_registry);
	saved_home     = nullptr;
	saved_registry = nullptr;
}

/* Redirect stdout or stderr (fd 1 or 2) into a file until redirect_end(). */
static void redirect_begin(int target_fd, const char *path)
{
	fflush(stdout);
	fflush(stderr);
	saved_fds[target_fd] = dup(target_fd);
	int fd               = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		dup2(fd, target_fd);
		close(fd);
	}
}

static void redirect_end(int target_fd)
{
	fflush(stdout);
	fflush(stderr);
	if (saved_fds[target_fd] >= 0) {
		dup2(saved_fds[target_fd], target_fd);
		close(saved_fds[target_fd]);
		saved_fds[target_fd] = -1;
	}
}

static void stderr_begin(const char *path)
{
	redirect_begin(STDERR_FILENO, path);
}

static void stderr_end(void)
{
	redirect_end(STDERR_FILENO);
}

static void stdout_begin(const char *path)
{
	redirect_begin(STDOUT_FILENO, path);
}

static void stdout_end(void)
{
	redirect_end(STDOUT_FILENO);
}

/* Run git in dir with up to five arguments (a nullptr ends the list). */
static i64 git_in(const char *dir, char *a1, char *a2, char *a3, char *a4, char *a5)
{
	char *argv[] = {
		"git", "-C", unconst(dir), "-c", "user.email=t@t", "-c", "user.name=t", "-c", "protocol.file.allow=always", a1,
		a2,    a3,   a4,           a5,   nullptr,
	};
	return run_command(argv, RUN_CMD_QUIET);
}

static sds git_head(const char *dir)
{
	char *argv[] = { "git", "-C", unconst(dir), "rev-parse", "HEAD", nullptr };
	sds   out    = run_command_capture(argv, RUN_CMD_QUIET);
	if (out == nullptr) {
		return sdsempty();
	}
	sdstrim(out, "\n");
	return out;
}

/* A git repository on branch main with library.toml and one source file. */
static void origin_make(const char *dir)
{
	mkdir(dir, 0755);
	sds src = join(dir, "src");
	mkdir(src, 0755);
	sdsfree(src);
	sds toml = join(dir, "library.toml");
	write_text(toml, "[package]\nname = \"origin\"\nversion = \"1.0.0\"\n");
	sdsfree(toml);
	sds file = join(dir, "src/origin.c");
	write_text(file, "int origin_one(void) { return 1; }\n");
	sdsfree(file);
	git_in(dir, "init", "-q", "-b", "main", nullptr);
	git_in(dir, "add", "-A", nullptr, nullptr, nullptr);
	git_in(dir, "commit", "-q", "-m", "one", nullptr);
}

/* Add one more commit to a repository made by origin_make(). */
static void origin_advance(const char *dir, const char *content)
{
	sds file = join(dir, "src/more.c");
	write_text(file, content);
	sdsfree(file);
	git_in(dir, "add", "-A", nullptr, nullptr, nullptr);
	git_in(dir, "commit", "-q", "-m", "more", nullptr);
}

/* Write Coffee.toml in the cwd: flat deps first, then [package], then tail. */
static void write_project(const char *name, const char *flat_deps, const char *tail)
{
	sds text = sdsempty();
	if (flat_deps != nullptr) {
		text = sdscatprintf(text, "%s\n", flat_deps);
	}
	text = sdscatprintf(text, "[package]\nname = \"%s\"\nversion = \"1.0.0\"\nedition = \"c23\"\n", name);
	if (tail != nullptr) {
		text = sdscatprintf(text, "\n%s", tail);
	}
	write_text("Coffee.toml", text);
	sdsfree(text);
	mkdir("src", 0755);
}

/* A dependency directory deps/<name> with a version and one source file. */
static void make_local_dep(const char *name, const char *version, const char *extra)
{
	mkdir("deps", 0755);
	sds dir = sdscatprintf(sdsempty(), "deps/%s", name);
	mkdir(dir, 0755);
	sds src = sdscatprintf(sdsempty(), "deps/%s/src", name);
	mkdir(src, 0755);
	sds file = sdscatprintf(sdsempty(), "deps/%s/src/%s.c", name, name);
	write_text(file, "int dep_fn(void) { return 0; }\n");
	sds toml = sdscatprintf(sdsempty(), "deps/%s/library.toml", name);
	sds text = sdscatprintf(sdsempty(), "[package]\nname = \"%s\"\nversion = \"%s\"\n%s", name, version,
	                        extra != nullptr ? extra : "");
	write_text(toml, text);
	sdsfree(text);
	sdsfree(toml);
	sdsfree(file);
	sdsfree(src);
	sdsfree(dir);
}

static manifest_t *parse_cwd_manifest(void)
{
	sds         path = sdsnew("Coffee.toml");
	manifest_t *m    = manifest_parse(path);
	sdsfree(path);
	return m;
}

/* Put a stub 'git' first on PATH: rev-parse prints nothing, others go to the real git. */
static bool stub_git_on(const char *root)
{
	char *which[] = { "sh", "-c", "command -v git", nullptr };
	sds   real    = run_command_capture(which, RUN_CMD_QUIET);
	if (real == nullptr) {
		return false;
	}
	sdstrim(real, "\n");
	sds dir = join(root, "stub");
	mkdir(dir, 0755);
	sds script = join(dir, "git");
	sds text   = sdscatprintf(sdsempty(),
	                          "#!/bin/sh\n"
	                          "for a in \"$@\"; do\n"
	                          "  if [ \"$a\" = rev-parse ]; then exit 0; fi\n"
	                          "done\n"
	                          "exec %s \"$@\"\n",
	                          real);
	write_text(script, text);
	chmod(script, 0755);
	saved_path = env_save("PATH");
	sds path   = sdscatprintf(sdsempty(), "%s:%s", dir, saved_path != nullptr ? saved_path : "/usr/bin:/bin");
	setenv("PATH", path, 1);
	sdsfree(path);
	sdsfree(text);
	sdsfree(script);
	sdsfree(dir);
	sdsfree(real);
	return true;
}

/* Put a stub 'rm' that always fails first on PATH. */
static void stub_rm_on(const char *root)
{
	sds dir = join(root, "stubrm");
	mkdir(dir, 0755);
	sds script = join(dir, "rm");
	write_text(script, "#!/bin/sh\nexit 1\n");
	chmod(script, 0755);
	saved_path = env_save("PATH");
	sds path   = sdscatprintf(sdsempty(), "%s:%s", dir, saved_path != nullptr ? saved_path : "/usr/bin:/bin");
	setenv("PATH", path, 1);
	sdsfree(path);
	sdsfree(script);
	sdsfree(dir);
}

/* Point CC at a fake compiler that fails when its arguments mention "broken". */
static void fake_cc_on(const char *root)
{
	sds script = join(root, "fakecc");
	write_text(script, "#!/bin/sh\ncase \"$*\" in *broken*) exit 1;; esac\nexit 0\n");
	chmod(script, 0755);
	saved_cc = env_save("CC");
	setenv("CC", script, 1);
	sdsfree(script);
}

static void fake_cc_off(void)
{
	env_restore("CC", saved_cc);
	saved_cc = nullptr;
}

static void stub_off(void)
{
	env_restore("PATH", saved_path);
	saved_path = nullptr;
}

/* ------------------------------------------------------------------ */
/* dep_graph.c                                                          */
/* ------------------------------------------------------------------ */

TEST(cov_dg_constraint_forms)
{
	sds root = tmp_new();
	enter_dir(root);
	make_local_dep("liba", "1.0.0", nullptr);
	make_local_dep("libb", "1.0.0", nullptr);
	make_local_dep("libc", "1.0.0", nullptr);
	make_local_dep("libd", "1.0.0", nullptr);
	make_local_dep("libe", "1.0.0", nullptr);
	write_project("constraints",
	              "dependencies = [\"liba > 0.5\", \"libb = 1.0  \", \"libc = \\\">= 0.5\\\"\", "
	              "\"libd = { version = \\\"1.0\\\" }\", \"libe\"]",
	              nullptr);

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	stderr_begin("warn.txt");
	dep_graph_t *g = dep_graph_create(m, nullptr, true);
	stderr_end();
	ASSERT(g != nullptr, "graph builds");

	ASSERT(dep_graph_find_node(g, "liba > 0.5") < 0, "invalid name is not a node");
	i64 b = dep_graph_find_node(g, "libb");
	i64 c = dep_graph_find_node(g, "libc");
	i64 d = dep_graph_find_node(g, "libd");
	i64 e = dep_graph_find_node(g, "libe");
	ASSERT(b > 0 && c > 0 && d > 0 && e > 0, "valid names become nodes");
	ASSERT(g->nodes[b].version_constraint != nullptr && strcmp(g->nodes[b].version_constraint, "1.0") == 0,
	       "unquoted constraint is trimmed");
	ASSERT(g->nodes[c].version_constraint != nullptr && strcmp(g->nodes[c].version_constraint, ">= 0.5") == 0,
	       "quoted constraint is unquoted");
	ASSERT(g->nodes[d].version_constraint == nullptr, "inline table gives no constraint");
	ASSERT(g->nodes[e].version_constraint == nullptr, "bare name gives no constraint");
	ASSERT(file_contains("warn.txt", "invalid dependency name"), "invalid name is reported");

	dep_graph_free(g);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_dg_conflicting_constraint_warns)
{
	sds root = tmp_new();
	enter_dir(root);
	make_local_dep("liba", "1.0.0", nullptr);
	make_local_dep("libb", "1.0.0", "\n[dependencies]\nliba = \">= 5.0.0\"\n");
	write_project("conflict", "dependencies = [\"liba\", \"libb\"]", nullptr);

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	stderr_begin("warn.txt");
	dep_graph_t *g = dep_graph_create(m, nullptr, true);
	stderr_end();
	ASSERT(g != nullptr, "graph builds");
	ASSERT(dep_graph_count(g) == 3, "root and two deps");
	ASSERT(file_contains("warn.txt", "libb requires liba >= 5.0.0 but 1.0.0 is resolved"), "conflict is reported");

	dep_graph_free(g);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_dg_source_forms)
{
	sds root = tmp_new();
	enter_dir(root);
	write_project("sources", nullptr,
	              "[dependencies]\n"
	              "tagged = { git = \"https://example.com/t.git\", tag = \"v1\" }\n"
	              "branched = { git = \"https://example.com/b.git\", branch = \"dev\" }\n"
	              "pinned = { git = \"https://example.com/p.git\", rev = \"abcdef1\" }\n"
	              "absolute = { path = \"/tmp/coffee-covdeps-absent\" }\n"
	              "plain = { version = \"1.0\" }\n"
	              "missing = { path = \"./does-not-exist\" }\n");

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	stderr_begin("warn.txt");
	dep_graph_t *g = dep_graph_create(m, nullptr, true);
	stderr_end();
	ASSERT(g != nullptr, "graph builds");

	i64 tagged   = dep_graph_find_node(g, "tagged");
	i64 branched = dep_graph_find_node(g, "branched");
	i64 pinned   = dep_graph_find_node(g, "pinned");
	i64 absolute = dep_graph_find_node(g, "absolute");
	i64 plain    = dep_graph_find_node(g, "plain");
	i64 missing  = dep_graph_find_node(g, "missing");
	ASSERT(tagged > 0 && branched > 0 && pinned > 0 && absolute > 0 && plain > 0 && missing > 0, "all nodes exist");
	ASSERT(strcmp(g->nodes[tagged].git_ref, "v1") == 0, "tag becomes the ref");
	ASSERT(strcmp(g->nodes[branched].git_ref, "dev") == 0, "branch becomes the ref");
	ASSERT(strcmp(g->nodes[pinned].git_ref, "abcdef1") == 0, "rev becomes the ref");
	ASSERT(strcmp(g->nodes[tagged].git_url, "https://example.com/t.git") == 0, "git url recorded");
	ASSERT(g->nodes[absolute].source_path != nullptr &&
	           strcmp(g->nodes[absolute].source_path, "/tmp/coffee-covdeps-absent") == 0,
	       "absolute root path is kept verbatim");
	ASSERT(g->nodes[plain].git_url == nullptr && g->nodes[plain].source_path == nullptr, "no source for version-only");
	ASSERT(g->nodes[missing].source_path == nullptr, "missing relative path is dropped");
	ASSERT(file_contains("warn.txt", "does not exist"), "missing path is reported");

	dep_graph_free(g);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_dg_version_fallbacks)
{
	sds root = tmp_new();
	enter_dir(root);
	mkdir("deps", 0755);
	mkdir("deps/nomanifest", 0755);
	make_local_dep("noversion", "1.0.0", nullptr);
	write_text("deps/noversion/library.toml", "[package]\nname = \"noversion\"\n");
	make_local_dep("broken", "1.0.0", nullptr);
	write_text("deps/broken/library.toml", "[package\nname = \n");
	make_local_dep("coffeeonly", "1.0.0", nullptr);
	remove("deps/coffeeonly/library.toml");
	write_text("deps/coffeeonly/Coffee.toml", "[package]\nname = \"coffeeonly\"\nversion = \"2.5.0\"\n");
	write_project("versions", "dependencies = [\"nomanifest\", \"noversion\", \"broken\", \"coffeeonly\"]", nullptr);

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g = dep_graph_create(m, nullptr, true);
	ASSERT(g != nullptr, "graph builds");

	i64 a = dep_graph_find_node(g, "nomanifest");
	i64 b = dep_graph_find_node(g, "noversion");
	i64 c = dep_graph_find_node(g, "broken");
	i64 d = dep_graph_find_node(g, "coffeeonly");
	ASSERT(a > 0 && b > 0 && c > 0 && d > 0, "all nodes exist");
	ASSERT(strcmp(g->nodes[a].version, "*") == 0, "no manifest gives wildcard");
	ASSERT(strcmp(g->nodes[b].version, "*") == 0, "manifest without version gives wildcard");
	ASSERT(strcmp(g->nodes[c].version, "*") == 0, "unparsable manifest gives wildcard");
	ASSERT(strcmp(g->nodes[d].version, "2.5.0") == 0, "Coffee.toml version is used");

	dep_graph_free(g);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_dg_invalid_root_name)
{
	manifest_t m;
	memset(&m, 0, sizeof(m));
	m.package.name = sdsnew("bad/name");

	sds root = tmp_new();
	enter_dir(root);
	stderr_begin("warn.txt");
	dep_graph_t *g = dep_graph_create(&m, nullptr, true);
	stderr_end();
	ASSERT(g == nullptr, "invalid root package name is refused");
	ASSERT(file_contains("warn.txt", "invalid root package name"), "refusal is reported");
	sdsfree(m.package.name);
	leave_dir(root);
	PASS();
}

TEST(cov_dg_nameless_structured_dep)
{
	sds root = tmp_new();
	enter_dir(root);
	mkdir("deps", 0755);

	dependency_t deps[2];
	memset(deps, 0, sizeof(deps));
	deps[1].name    = sdsnew("libs");
	deps[1].version = sdsnew("1.0");
	manifest_t m;
	memset(&m, 0, sizeof(m));
	m.package.name            = sdsnew("nameless");
	m.dependencies.deps       = deps;
	m.dependencies.deps_count = 2;
	dep_graph_t *g            = dep_graph_create(&m, nullptr, true);
	ASSERT(g != nullptr, "graph builds");
	ASSERT(dep_graph_count(g) == 2, "nameless entry is skipped");
	ASSERT(dep_graph_find_node(g, "libs") == 1, "named entry is registered");
	ASSERT(strcmp(g->nodes[0].version, "*") == 0, "missing root version gives wildcard");

	dep_graph_free(g);
	sdsfree(deps[1].name);
	sdsfree(deps[1].version);
	sdsfree(m.package.name);
	leave_dir(root);
	PASS();
}

TEST(cov_dg_transitive_self_reference)
{
	sds root = tmp_new();
	enter_dir(root);
	make_local_dep("liba", "1.0.0", "\n[dependencies]\nliba = { version = \"1.0\" }\nlibb = { version = \"1.0\" }\n");
	write_project("selfref", "dependencies = [\"liba\"]", nullptr);

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g = dep_graph_create(m, nullptr, true);
	ASSERT(g != nullptr, "graph builds");
	ASSERT(dep_graph_find_node(g, "libb") > 0, "other transitive dep is registered");
	ASSERT(dep_graph_count(g) == 3, "self reference adds no node");

	dep_graph_free(g);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_dg_query_edge_cases)
{
	ASSERT(dep_graph_find_node(nullptr, "x") == -1, "null graph");
	dep_graph_t empty;
	memset(&empty, 0, sizeof(empty));
	ASSERT(dep_graph_find_node(&empty, nullptr) == -1, "null name");
	ASSERT(dep_graph_names(&empty, nullptr) == nullptr, "null count pointer");
	ASSERT(dep_graph_names(nullptr, nullptr) == nullptr, "null graph names");
	size_t count = 99;
	sds   *names = dep_graph_names(&empty, &count);
	ASSERT(count == 0, "empty graph has no names");
	safe_free(names);
	dep_graph_free(nullptr);
	PASS();
}

/* ------------------------------------------------------------------ */
/* dep_graph_cache.c                                                    */
/* ------------------------------------------------------------------ */

/* Nanosecond mtime of a file, as dep_graph_get() computes it (0 if absent). */
static i64 mtime_ns(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		return 0;
	}
	return ((i64)st.st_mtim.tv_sec * 1'000'000'000) + st.st_mtim.tv_nsec;
}

/*
 * Write .coffee/build-cache/<name>.graph with the current mtimes.  prefix is
 * placed before [metadata] (so it holds top-level keys), body after it.
 */
static void write_cache(const char *name, const char *prefix, const char *meta_extra, const char *body)
{
	mkdir(".coffee", 0755);
	mkdir(".coffee/build-cache", 0755);
	sds path = sdscatprintf(sdsempty(), ".coffee/build-cache/%s.graph", name);
	sds text = sdscatprintf(sdsempty(), "%s[metadata]\ncoffee_toml_mtime = %lld\ncoffee_lock_mtime = 0\n%s\n%s", prefix,
	                        (long long)mtime_ns("Coffee.toml"), meta_extra, body);
	write_text(path, text);
	sdsfree(text);
	sdsfree(path);
}

TEST(cov_cache_load_all_fields)
{
	sds root = tmp_new();
	enter_dir(root);
	write_project("cachefull", nullptr, nullptr);
	write_cache("cachefull", "", "cache_version = 3\noffline = true",
	            "[[nodes]]\nname = \"cachefull\"\npath = \".\"\nversion = \"1.0.0\"\nis_git = false\n\n"
	            "[[nodes]]\nname = \"dep\"\npath = \"deps/dep\"\nversion = \"2.0.0\"\nversion_constraint = \">= 1.0\"\n"
	            "commit = \"abc123\"\nis_git = true\ngit_ref = \"v2\"\ngit_url = \"https://example.com/d.git\"\n"
	            "source_path = \"/src/dep\"\nflags = \"-Ideps/dep/include\"\n"
	            "sources = [\"deps/dep/src/a.c\", \"deps/dep/src/b.c\"]\n");

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g = dep_graph_get(m, nullptr, false, ".");
	ASSERT(g != nullptr, "cache is loaded");
	ASSERT(g->offline, "offline flag comes from the cache");
	ASSERT(dep_graph_count(g) == 2, "both nodes loaded");
	i64 d = dep_graph_find_node(g, "dep");
	ASSERT(d == 1, "dep node loaded");
	ASSERT(strcmp(g->nodes[d].version_constraint, ">= 1.0") == 0, "constraint loaded");
	ASSERT(strcmp(dep_graph_commit(g, "dep"), "abc123") == 0, "commit loaded");
	ASSERT(dep_graph_is_git(g, "dep"), "git flag loaded");
	ASSERT(strcmp(dep_graph_git_ref(g, "dep"), "v2") == 0, "ref loaded");
	ASSERT(strcmp(g->nodes[d].git_url, "https://example.com/d.git") == 0, "url loaded");
	ASSERT(strcmp(g->nodes[d].source_path, "/src/dep") == 0, "source path loaded");
	sds flags = dep_graph_flags(g, "dep");
	ASSERT(strcmp(flags, "-Ideps/dep/include") == 0, "flags loaded");
	sdsfree(flags);
	size_t     nsrc = 0;
	const sds *srcs = dep_graph_sources(g, "dep", &nsrc);
	ASSERT(nsrc == 2 && srcs != nullptr && strcmp(srcs[1], "deps/dep/src/b.c") == 0, "sources loaded");

	dep_graph_free(g);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_cache_minimal_node_defaults)
{
	sds root = tmp_new();
	enter_dir(root);
	write_project("cachemin", nullptr, nullptr);
	/* No offline key and no is_git key: both default to false. */
	write_cache("cachemin", "", "cache_version = 3", "[[nodes]]\nname = \"cachemin\"\n");

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g != nullptr, "cache is loaded");
	ASSERT(!g->offline, "offline defaults to false");
	ASSERT(!g->nodes[0].is_git, "is_git defaults to false");
	ASSERT(g->nodes[0].path == nullptr, "no path stored");

	dep_graph_free(g);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

/* An unusable cache is ignored and the graph is rebuilt from the manifest. */
static i64 cache_is_rebuilt(const char *name, const char *prefix, const char *meta_extra, const char *body,
                            bool with_metadata)
{
	write_project(name, nullptr, nullptr);
	if (with_metadata) {
		write_cache(name, prefix, meta_extra, body);
	} else {
		mkdir(".coffee", 0755);
		mkdir(".coffee/build-cache", 0755);
		sds path = sdscatprintf(sdsempty(), ".coffee/build-cache/%s.graph", name);
		write_text(path, body);
		sdsfree(path);
	}
	manifest_t *m = parse_cwd_manifest();
	if (m == nullptr) {
		return 0;
	}
	dep_graph_t *g          = dep_graph_get(m, nullptr, true, ".");
	sds          cache_path = sdscatprintf(sdsempty(), ".coffee/build-cache/%s.graph", name);
	i64          ok         = g != nullptr && dep_graph_count(g) == 1 && strcmp(dep_graph_node_name(g, 0), name) == 0 &&
	                          file_contains(cache_path, "version = \"1.0.0\"");
	sdsfree(cache_path);
	dep_graph_free(g);
	manifest_free(m);
	return ok;
}

TEST(cov_cache_rejects_bad_caches)
{
	sds root = tmp_new();
	enter_dir(root);
	const char *node = "[[nodes]]\nname = \"x\"\n";
	ASSERT(cache_is_rebuilt("nometa", "", "", node, false), "missing metadata");
	ASSERT(cache_is_rebuilt("oldver", "", "cache_version = 2", node, true), "old cache version");
	ASSERT(cache_is_rebuilt("nover", "", "", node, true), "no cache version");
	ASSERT(cache_is_rebuilt("nonodes", "", "cache_version = 3", "", true), "no node array");
	ASSERT(cache_is_rebuilt("emptynodes", "nodes = []\n", "cache_version = 3", "", true), "empty node array");
	ASSERT(cache_is_rebuilt("notabs", "nodes = [1, 2]\n", "cache_version = 3", "", true), "only non-table nodes");
	leave_dir(root);
	PASS();
}

TEST(cov_cache_rejects_stale_mtimes)
{
	sds root = tmp_new();
	enter_dir(root);
	write_project("stale", nullptr, nullptr);
	mkdir(".coffee", 0755);
	mkdir(".coffee/build-cache", 0755);
	write_text(".coffee/build-cache/stale.graph",
	           "[metadata]\ncoffee_toml_mtime = 5\ncoffee_lock_mtime = 0\ncache_version = 3\n"
	           "[[nodes]]\nname = \"x\"\n");
	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g != nullptr && strcmp(dep_graph_node_name(g, 0), "stale") == 0, "wrong toml mtime is a miss");
	dep_graph_free(g);

	/* Missing lock mtime key in an otherwise valid cache. */
	char text[512];
	snprintf_safe(text, sizeof(text),
	              "[metadata]\ncoffee_toml_mtime = %lld\ncache_version = 3\n[[nodes]]\nname = \"x\"\n",
	              (long long)mtime_ns("Coffee.toml"));
	write_text(".coffee/build-cache/stale.graph", text);
	g = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g != nullptr && strcmp(dep_graph_node_name(g, 0), "stale") == 0, "missing lock mtime is a miss");
	dep_graph_free(g);

	/* Wrong lock mtime. */
	write_text("Coffee.lock", "version = 1\n");
	snprintf_safe(text, sizeof(text),
	              "[metadata]\ncoffee_toml_mtime = %lld\ncoffee_lock_mtime = 7\ncache_version = 3\n"
	              "[[nodes]]\nname = \"x\"\n",
	              (long long)mtime_ns("Coffee.toml"));
	write_text(".coffee/build-cache/stale.graph", text);
	g = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g != nullptr && strcmp(dep_graph_node_name(g, 0), "stale") == 0, "wrong lock mtime is a miss");
	dep_graph_free(g);

	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_cache_write_all_fields_roundtrip)
{
	sds root = tmp_new();
	enter_dir(root);
	make_local_dep("liba", "1.0.0", nullptr);
	write_project("cacheroundtrip", nullptr,
	              "[dependencies]\n"
	              "liba = \">= 0.5\"\n"
	              "tagged = { git = \"https://example.com/t.git\", tag = \"v1\" }\n"
	              "absolute = { path = \"/tmp/coffee-covdeps-absent\" }\n");

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g1 = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g1 != nullptr, "first call builds and writes the cache");
	ASSERT(safe_access(".coffee/build-cache/cacheroundtrip.graph", F_OK) == 0, "cache file written");
	ASSERT(file_contains(".coffee/build-cache/cacheroundtrip.graph", "version_constraint = \">= 0.5\""),
	       "constraint serialized");
	ASSERT(file_contains(".coffee/build-cache/cacheroundtrip.graph", "git_ref = \"v1\""), "ref serialized");
	ASSERT(file_contains(".coffee/build-cache/cacheroundtrip.graph", "git_url = \"https://example.com/t.git\""),
	       "url serialized");
	ASSERT(file_contains(".coffee/build-cache/cacheroundtrip.graph", "source_path = \"/tmp/coffee-covdeps-absent\""),
	       "source path serialized");

	dep_graph_t *g2 = dep_graph_get(m, nullptr, false, ".");
	ASSERT(g2 != nullptr, "second call loads the cache");
	ASSERT(g2->offline, "cache preserves the offline flag of the first build");
	ASSERT(dep_graph_count(g2) == dep_graph_count(g1), "same node count");
	i64 t = dep_graph_find_node(g2, "tagged");
	ASSERT(t > 0 && strcmp(g2->nodes[t].git_ref, "v1") == 0, "ref survives the roundtrip");
	i64 a = dep_graph_find_node(g2, "liba");
	ASSERT(a > 0 && strcmp(g2->nodes[a].version_constraint, ">= 0.5") == 0, "constraint survives the roundtrip");

	dep_graph_free(g2);
	dep_graph_free(g1);
	manifest_free(m);
	leave_dir(root);
	PASS();
}

TEST(cov_cache_write_failures)
{
	sds root = tmp_new();
	enter_dir(root);
	write_project("cachefail", nullptr, nullptr);
	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");

	/* rename() onto a directory fails: the graph is still returned. */
	mkdir(".coffee", 0755);
	mkdir(".coffee/build-cache", 0755);
	mkdir(".coffee/build-cache/cachefail.graph", 0755);
	dep_graph_t *g = dep_graph_get(m, nullptr, true, ".");
	ASSERT(g != nullptr, "graph survives a failed rename");
	dep_graph_free(g);
	rmdir(".coffee/build-cache/cachefail.graph");

	/* An unwritable cache directory makes the temp file creation fail. */
	chmod(".coffee/build-cache", 0555);
	g = dep_graph_get(m, nullptr, true, ".");
	chmod(".coffee/build-cache", 0755);
	ASSERT(g != nullptr, "graph survives an unwritable cache dir");
	ASSERT(safe_access(".coffee/build-cache/cachefail.graph", F_OK) != 0, "no cache written");
	dep_graph_free(g);

	/* fsync() fails on a FIFO: hand the writer one with a reader attached. */
	sds fifo = sdscatprintf(sdsempty(), ".coffee/build-cache/cachefail.graph.%d.tmp", (int)getpid());
	ASSERT(mkfifo(fifo, 0600) == 0, "mkfifo");
	int reader = open(fifo, O_RDWR | O_NONBLOCK);
	ASSERT(reader >= 0, "open fifo reader");
	g = dep_graph_get(m, nullptr, true, ".");
	close(reader);
	ASSERT(g != nullptr, "graph survives a failed fsync");
	ASSERT(safe_access(".coffee/build-cache/cachefail.graph", F_OK) != 0, "no cache written after fsync failure");
	dep_graph_free(g);
	sdsfree(fifo);

	/* A write beyond RLIMIT_FSIZE makes fflush() fail. */
	struct rlimit old_limit;
	getrlimit(RLIMIT_FSIZE, &old_limit);
	signal(SIGXFSZ, SIG_IGN);
	struct rlimit tiny = { .rlim_cur = 8, .rlim_max = old_limit.rlim_max };
	setrlimit(RLIMIT_FSIZE, &tiny);
	g = dep_graph_get(m, nullptr, true, ".");
	setrlimit(RLIMIT_FSIZE, &old_limit);
	signal(SIGXFSZ, SIG_DFL);
	ASSERT(g != nullptr, "graph survives a failed flush");
	ASSERT(safe_access(".coffee/build-cache/cachefail.graph", F_OK) != 0, "no cache written after flush failure");
	dep_graph_free(g);

	manifest_free(m);
	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* dep_graph_git.c                                                      */
/* ------------------------------------------------------------------ */

/*
 * Create root/origin (a git repo) and root/proj with a manifest declaring
 * lib = { git = <origin> } plus a clone of origin in proj/deps/lib, then
 * chdir into proj.  Returns the origin path (caller frees).
 */
static sds git_project_enter(const char *root, const char *dep_line)
{
	sds origin = join(root, "origin");
	origin_make(origin);
	sds proj = join(root, "proj");
	mkdir(proj, 0755);
	enter_dir(proj);
	sdsfree(proj);
	sds tail = sdscatprintf(sdsempty(), "[dependencies]\n%s\n", dep_line);
	write_project("gitproj", nullptr, tail);
	sdsfree(tail);
	mkdir("deps", 0755);
	char *clone_argv[] = { "git", "clone", "-q", "--", origin, "deps/lib", nullptr };
	run_command(clone_argv, RUN_CMD_QUIET);
	return origin;
}

static sds lib_line(const char *origin)
{
	return sdscatprintf(sdsempty(), "lib = { git = \"%s\" }", origin);
}

TEST(cov_git_compare_remote_states)
{
	sds root   = tmp_new();
	sds line   = nullptr;
	sds origin = join(root, "origin");
	sdsfree(origin);
	origin = join(root, "origin");
	line   = lib_line(origin);
	sdsfree(origin);
	origin = git_project_enter(root, line);
	sdsfree(line);

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph builds");
	i64 lib = dep_graph_find_node(g, "lib");
	ASSERT(lib > 0 && g->nodes[lib].is_git, "lib is a git node");

	sds behind = nullptr;
	ASSERT(dep_graph_compare_remote(g, "lib", &behind) == 0, "fresh clone is up to date");
	ASSERT(behind != nullptr && strcmp(behind, "up-to-date") == 0, "up-to-date message");
	sdsfree(behind);
	ASSERT(dep_graph_compare_remote(g, "lib", nullptr) == 0, "message is optional");

	origin_advance(origin, "int more_one(void) { return 1; }\n");
	behind = nullptr;
	ASSERT(dep_graph_compare_remote(g, "lib", &behind) == 1, "one commit behind");
	ASSERT(behind != nullptr && strcmp(behind, "1 commit behind") == 0, "singular message");
	sdsfree(behind);

	origin_advance(origin, "int more_two(void) { return 2; }\n");
	origin_advance(origin, "int more_three(void) { return 3; }\n");
	behind  = nullptr;
	i64 ret = dep_graph_compare_remote(g, "lib", &behind);
	ASSERT(ret >= 1, "still behind");
	ASSERT(behind != nullptr && strstr(behind, "behind") != nullptr, "behind message");
	sdsfree(behind);
	ASSERT(dep_graph_compare_remote(g, "lib", nullptr) >= 1, "behind without message");

	/* A ref that does not exist remotely makes rev-list fail. */
	sdsfree(g->nodes[lib].git_ref);
	g->nodes[lib].git_ref = sdsnew("origin/no-such-branch");
	behind                = nullptr;
	ASSERT(dep_graph_compare_remote(g, "lib", &behind) == -1, "unknown ref fails");
	ASSERT(behind != nullptr && strcmp(behind, "(rev-list failed)") == 0, "rev-list failure message");
	sdsfree(behind);
	ASSERT(dep_graph_compare_remote(g, "lib", nullptr) == -1, "unknown ref fails without message");

	/* A ref git would take for an option is refused before any command runs. */
	sdsfree(g->nodes[lib].git_ref);
	g->nodes[lib].git_ref = sdsnew("-evil");
	behind                = nullptr;
	ASSERT(dep_graph_compare_remote(g, "lib", &behind) == -1, "option-like ref is refused");
	ASSERT(behind != nullptr && strcmp(behind, "(invalid ref)") == 0, "invalid ref message");
	sdsfree(behind);
	ASSERT(dep_graph_compare_remote(g, "lib", nullptr) == -1, "invalid ref without message");

	/* Offline graphs never touch the network. */
	g->offline = true;
	behind     = nullptr;
	sdsfree(g->nodes[lib].git_ref);
	g->nodes[lib].git_ref = nullptr;
	ASSERT(dep_graph_compare_remote(g, "lib", &behind) == -1, "offline graph is refused");
	ASSERT(behind != nullptr && strcmp(behind, "(offline)") == 0, "offline message");
	sdsfree(behind);
	ASSERT(dep_graph_compare_remote(g, "lib", nullptr) == -1, "offline without message");
	g->offline = false;

	/* A dead remote makes the fetch fail. */
	git_in("deps/lib", "remote", "set-url", "origin", "/nonexistent/coffee-covdeps-remote", nullptr);
	behind = nullptr;
	ASSERT(dep_graph_compare_remote(g, "lib", &behind) == -1, "dead remote fails");
	ASSERT(behind != nullptr && strcmp(behind, "(fetch failed)") == 0, "fetch failure message");
	sdsfree(behind);
	ASSERT(dep_graph_compare_remote(g, "lib", nullptr) == -1, "dead remote without message");

	/* Nodes that are not git checkouts are refused. */
	g->nodes[lib].is_git = false;
	ASSERT(dep_graph_compare_remote(g, "lib", nullptr) == -1, "non-git node is refused");
	ASSERT(dep_graph_compare_remote(g, "absent", nullptr) == -1, "unknown node is refused");

	dep_graph_free(g);
	manifest_free(m);
	sdsfree(origin);
	leave_dir(root);
	PASS();
}

TEST(cov_git_fetch_git_variants)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	sds line   = lib_line(origin);
	sdsfree(origin);
	origin = git_project_enter(root, line);
	sdsfree(line);
	git_in(origin, "tag", "v1", nullptr, nullptr, nullptr);

	manifest_t *m = parse_cwd_manifest();
	ASSERT(m != nullptr, "manifest parses");
	dep_graph_t *g = dep_graph_create(m, nullptr, false);
	ASSERT(g != nullptr, "graph builds");
	i64 lib = dep_graph_find_node(g, "lib");
	ASSERT(lib > 0 && g->nodes[lib].is_git, "lib is a git node");

	/* No ref: fetch and reset to origin/HEAD. */
	origin_advance(origin, "int more_one(void) { return 1; }\n");
	sds head = git_head(origin);
	stdout_begin("out.txt");
	i64 ret = dep_graph_fetch_git(g, "lib", true);
	stdout_end();
	ASSERT(ret == 0, "fetch without ref succeeds");
	ASSERT(file_contains("out.txt", "Fetching lib..."), "verbose message without ref");
	ASSERT(strcmp(dep_graph_commit(g, "lib"), head) == 0, "node commit follows origin/HEAD");
	ASSERT(dep_graph_fetch_git(g, "lib", false) == 0, "quiet fetch succeeds");
	sdsfree(head);

	/* Branch or tag ref. */
	sdsfree(g->nodes[lib].git_ref);
	g->nodes[lib].git_ref = sdsnew("v1");
	stdout_begin("out.txt");
	ret = dep_graph_fetch_git(g, "lib", true);
	stdout_end();
	ASSERT(ret == 0, "fetch of a tag succeeds");
	ASSERT(file_contains("out.txt", "Fetching lib (v1)..."), "verbose message with ref");
	ASSERT(dep_graph_fetch_git(g, "lib", false) == 0, "quiet fetch of a tag succeeds");

	/* Rev ref, verbose. */
	sds first = git_head("deps/lib");
	sdsfree(g->nodes[lib].git_ref);
	g->nodes[lib].git_ref = sdsdup(first);
	stdout_begin("out.txt");
	ret = dep_graph_fetch_git(g, "lib", true);
	stdout_end();
	ASSERT(ret == 0, "fetch of a rev succeeds");
	ASSERT(file_contains("out.txt", first), "verbose message names the rev");
	sdsfree(first);

	/* A ref that does not exist remotely fails. */
	sdsfree(g->nodes[lib].git_ref);
	g->nodes[lib].git_ref = sdsnew("no-such-branch");
	ASSERT(dep_graph_fetch_git(g, "lib", false) != 0, "unknown ref fails");

	/* An option-like ref is refused. */
	sdsfree(g->nodes[lib].git_ref);
	g->nodes[lib].git_ref = sdsnew("-evil");
	stderr_begin("err.txt");
	ret = dep_graph_fetch_git(g, "lib", false);
	stderr_end();
	ASSERT(ret == -1, "option-like ref is refused");
	ASSERT(file_contains("err.txt", "invalid git ref '-evil'"), "refusal is reported");

	/* Refused arguments. */
	ASSERT(dep_graph_fetch_git(g, "absent", false) == -1, "unknown node");
	g->nodes[lib].is_git = false;
	ASSERT(dep_graph_fetch_git(g, "lib", false) == -1, "non-git node");
	g->nodes[lib].is_git = true;
	sdsfree(g->nodes[lib].path);
	g->nodes[lib].path = nullptr;
	ASSERT(dep_graph_fetch_git(g, "lib", false) == -1, "node without path");
	ASSERT(dep_graph_fetch_git(g, nullptr, false) == -1, "null name");

	dep_graph_free(g);
	manifest_free(m);
	sdsfree(origin);
	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* Command helpers                                                      */
/* ------------------------------------------------------------------ */

/* Run a command handler with the command name and up to two arguments. */
static i64 run_cmd(i64 (*handler)(options *), char *cmd, char *arg1, char *arg2)
{
	char   *inputs[] = { cmd, arg1, arg2, nullptr };
	options opt;
	memset(&opt, 0, sizeof(opt));
	opt.inputs     = inputs;
	opt.inputs_num = 1;
	if (arg1 != nullptr) {
		opt.inputs_num++;
	}
	if (arg2 != nullptr) {
		opt.inputs_num++;
	}
	return handler(&opt);
}

/* Enter root/proj with a manifest holding the given [dependencies] tail. */
static void proj_enter(const char *root, const char *name, const char *tail)
{
	sds proj = join(root, "proj");
	mkdir(proj, 0755);
	enter_dir(proj);
	sdsfree(proj);
	write_project(name, nullptr, tail);
}

static sds lock_text(void)
{
	return read_text("Coffee.lock");
}

/* ------------------------------------------------------------------ */
/* fetch                                                                */
/* ------------------------------------------------------------------ */

TEST(cov_fetch_git_tag_clone_then_update)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	git_in(origin, "tag", "v1", nullptr, nullptr, nullptr);
	sds first = git_head(origin);
	origin_advance(origin, "int later(void) { return 2; }\n");
	sandbox_on(root);
	sds tail = sdscatprintf(sdsempty(), "[dependencies]\nlib = { git = \"%s\", tag = \"v1\" }\n", origin);
	proj_enter(root, "tagproj", tail);
	sdsfree(tail);

	options opt;
	memset(&opt, 0, sizeof(opt));
	char *inputs[] = { "fetch", nullptr };
	opt.inputs     = inputs;
	opt.inputs_num = 1;
	opt.verbose    = true;

	stdout_begin("out.txt");
	i64 ret = handle_fetch(&opt);
	stdout_end();
	ASSERT(ret == 0, "first fetch clones the tag");
	ASSERT(file_contains("out.txt", "Cloning lib..."), "clone is reported");
	ASSERT(safe_access("deps/lib/.git", F_OK) == 0, "dependency is materialized");
	sds lock = lock_text();
	ASSERT(strstr(lock, first) != nullptr, "lockfile pins the tagged commit");
	sdsfree(lock);

	stdout_begin("out.txt");
	ret = handle_fetch(&opt);
	stdout_end();
	ASSERT(ret == 0, "second fetch updates the existing clone");
	ASSERT(file_contains("out.txt", "Updating lib..."), "update is reported");
	lock = lock_text();
	ASSERT(strstr(lock, first) != nullptr, "lockfile still pins the tagged commit");
	sdsfree(lock);

	sdsfree(first);
	sdsfree(origin);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_git_branch_and_default_ref)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	sandbox_on(root);
	sds tail = sdscatprintf(sdsempty(),
	                        "[dependencies]\nonbranch = { git = \"%s\", branch = \"main\" }\n"
	                        "plain = { git = \"%s\" }\n",
	                        origin, origin);
	proj_enter(root, "branchproj", tail);
	sdsfree(tail);

	ASSERT(run_cmd(handle_fetch, "fetch", nullptr, nullptr) == 0, "branch and default clones succeed");
	ASSERT(safe_access("deps/onbranch/.git", F_OK) == 0, "branch dependency materialized");
	ASSERT(safe_access("deps/plain/.git", F_OK) == 0, "default dependency materialized");

	/* Updating existing clones takes the ref and no-ref update paths. */
	origin_advance(origin, "int later(void) { return 2; }\n");
	ASSERT(run_cmd(handle_fetch, "fetch", nullptr, nullptr) == 0, "second fetch succeeds");
	sds head = git_head(origin);
	sds lock = lock_text();
	ASSERT(strstr(lock, head) != nullptr, "lockfile pins the new head");
	sdsfree(lock);
	sdsfree(head);

	sdsfree(origin);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_git_failures)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	sandbox_on(root);
	sds tail = sdscatprintf(sdsempty(), "[dependencies]\nbad = { git = \"%s\", branch = \"-evil\" }\n", origin);
	proj_enter(root, "badrefproj", tail);
	sdsfree(tail);

	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_fetch, "fetch", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "option-like ref makes the fetch fail");
	ASSERT(file_contains("err.txt", "invalid git ref '-evil'"), "bad ref is reported");
	ASSERT(file_contains("err.txt", "Coffee.lock left unchanged"), "no partial lockfile");
	ASSERT(safe_access("Coffee.lock", F_OK) != 0, "no lockfile written");

	/* A clone that cannot succeed. */
	write_project("badurlproj", nullptr, "[dependencies]\ngone = { git = \"/nonexistent/coffee-covdeps-repo\" }\n");
	stderr_begin("err.txt");
	ret = run_cmd(handle_fetch, "fetch", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unreachable repository makes the fetch fail");
	ASSERT(file_contains("err.txt", "Failed to clone git dependency 'gone'"), "clone failure is reported");

	sdsfree(origin);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_invalid_root_name)
{
	sds root = tmp_new();
	enter_dir(root);
	sandbox_on(root);
	write_project("bad name", nullptr, nullptr);
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_fetch, "fetch", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "fetch refuses an unresolvable graph");
	ASSERT(file_contains("err.txt", "Could not resolve dependency graph"), "refusal is reported");
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_path_dep_variants)
{
	sds root = tmp_new();
	sandbox_on(root);
	proj_enter(root, "pathproj",
	           "[dependencies]\n"
	           "good = { path = \"./vend/good\" }\n"
	           "relmissing = { path = \"./vend/absent\" }\n"
	           "absmissing = { path = \"/tmp/coffee-covdeps-surely-absent\" }\n");
	mkdir("vend", 0755);
	mkdir("vend/good", 0755);
	write_text("vend/good/library.toml", "[package]\nname = \"good\"\nversion = \"1.0.0\"\n");

	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_fetch, "fetch", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "missing path dependencies fail the fetch");
	ASSERT(file_contains("err.txt", "path './vend/absent' for 'relmissing' does not exist"), "relative path reported");
	ASSERT(file_contains("err.txt", "path '/tmp/coffee-covdeps-surely-absent' for 'absmissing' does not exist"),
	       "absolute path reported");
	ASSERT(safe_access("deps/good/library.toml", F_OK) == 0, "good path dependency is linked");

	/* With only the good dependency, a re-fetch replaces the existing link. */
	write_project("pathproj", nullptr, "[dependencies]\ngood = { path = \"./vend/good\" }\n");
	ASSERT(run_cmd(handle_fetch, "fetch", nullptr, nullptr) == 0, "re-fetch succeeds");
	sds lock = lock_text();
	ASSERT(strstr(lock, "name = \"good\"") != nullptr, "lockfile lists the dependency");
	sdsfree(lock);

	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_transitive_path_dep)
{
	sds root = tmp_new();
	sandbox_on(root);
	proj_enter(root, "transproj", "[dependencies]\nparent = { path = \"./vend/parent\" }\n");
	mkdir("vend", 0755);
	mkdir("vend/parent", 0755);
	mkdir("vend/parent/child", 0755);
	write_text("vend/parent/Coffee.toml", "[package]\nname = \"parent\"\nversion = \"1.0.0\"\n\n[dependencies]\n"
	                                      "child = { path = \"./child\" }\n");
	write_text("vend/parent/child/library.toml", "[package]\nname = \"child\"\nversion = \"1.0.0\"\n");

	ASSERT(run_cmd(handle_fetch, "fetch", nullptr, nullptr) == 0, "fetch resolves the nested path dependency");
	ASSERT(safe_access("deps/child/library.toml", F_OK) == 0, "child is linked from its parent's source path");
	sds lock = lock_text();
	ASSERT(strstr(lock, "name = \"child\"") != nullptr, "child is recorded in the lockfile");
	sdsfree(lock);

	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_registry_failures)
{
	sds root = tmp_new();
	sandbox_on(root);
	proj_enter(root, "regproj", "[dependencies]\nregdep = \"1.0\"\n");

	/* Offline: a registry dependency cannot be resolved. */
	options opt;
	memset(&opt, 0, sizeof(opt));
	char *inputs[] = { "fetch", nullptr };
	opt.inputs     = inputs;
	opt.inputs_num = 1;
	opt.offline    = true;
	stderr_begin("err.txt");
	i64 ret = handle_fetch(&opt);
	stderr_end();
	ASSERT(ret == 1, "offline registry fetch fails");
	ASSERT(file_contains("err.txt", "offline mode"), "offline refusal is reported");

	/* The recipe exists but its source cannot be cloned. */
	sds   recipe_dir   = join(root, "registry/recipes/r/regdep");
	char *mkdir_argv[] = { "mkdir", "-p", recipe_dir, nullptr };
	run_command(mkdir_argv, RUN_CMD_QUIET);
	sds recipe = join(recipe_dir, "library.toml");
	write_text(recipe, "version = \"1.0.0\"\nrecipe_url = \"/nonexistent/coffee-covdeps-recipe\"\n");
	stderr_begin("err.txt");
	ret = run_cmd(handle_fetch, "fetch", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unclonable recipe fails the fetch");
	ASSERT(safe_access("deps/regdep", F_OK) != 0, "nothing is linked");

	sdsfree(recipe);
	sdsfree(recipe_dir);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_offline_git_deps)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	sandbox_on(root);
	sds tail =
	    sdscatprintf(sdsempty(), "[dependencies]\nlib = { git = \"%s\" }\ngone = { git = \"%s\" }\n", origin, origin);
	proj_enter(root, "offproj", tail);
	sdsfree(tail);
	mkdir("deps", 0755);
	char *clone_argv[] = { "git", "clone", "-q", "--", origin, "deps/lib", nullptr };
	run_command(clone_argv, RUN_CMD_QUIET);

	options opt;
	memset(&opt, 0, sizeof(opt));
	char *inputs[] = { "fetch", nullptr };
	opt.inputs     = inputs;
	opt.inputs_num = 1;
	opt.offline    = true;
	stderr_begin("err.txt");
	i64 ret = handle_fetch(&opt);
	stderr_end();
	ASSERT(ret == 1, "a missing git dependency fails the offline fetch");
	ASSERT(file_contains("err.txt", "cannot fetch git dependency 'gone' in offline mode"), "failure is reported");

	tail = sdscatprintf(sdsempty(), "[dependencies]\nlib = { git = \"%s\" }\n", origin);
	write_project("offproj", nullptr, tail);
	sdsfree(tail);
	ASSERT(handle_fetch(&opt) == 0, "an on-disk git dependency passes the offline fetch");
	sds head = git_head("deps/lib");
	sds lock = lock_text();
	ASSERT(strstr(lock, head) != nullptr, "lockfile pins the on-disk commit");
	sdsfree(lock);
	sdsfree(head);

	sdsfree(origin);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_head_commit_edge_cases)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	sandbox_on(root);
	sds tail = sdscatprintf(sdsempty(), "[dependencies]\nfake = { git = \"%s\" }\n", origin);
	proj_enter(root, "headproj", tail);
	sdsfree(tail);

	options opt;
	memset(&opt, 0, sizeof(opt));
	char *inputs[] = { "fetch", nullptr };
	opt.inputs     = inputs;
	opt.inputs_num = 1;
	opt.offline    = true;

	/* A .git directory that is not a repository: rev-parse fails, no commit recorded. */
	mkdir("deps", 0755);
	mkdir("deps/fake", 0755);
	mkdir("deps/fake/.git", 0755);
	ASSERT(handle_fetch(&opt) == 0, "offline fetch tolerates a broken checkout");
	sds lock = lock_text();
	ASSERT(strstr(lock, "name = \"fake\"") != nullptr, "dependency is recorded");
	ASSERT(strstr(lock, "commit") == nullptr, "no commit is recorded");
	sdsfree(lock);

	/* A real checkout whose rev-parse prints nothing. */
	test_remove_tree("deps/fake");
	char *clone_argv[] = { "git", "clone", "-q", "--", origin, "deps/fake", nullptr };
	run_command(clone_argv, RUN_CMD_QUIET);
	ASSERT(stub_git_on(root), "git stub installed");
	i64 ret = handle_fetch(&opt);
	stub_off();
	ASSERT(ret == 0, "offline fetch tolerates an empty rev-parse");
	lock = lock_text();
	ASSERT(strstr(lock, "commit") == nullptr, "empty rev-parse records no commit");
	sdsfree(lock);

	sdsfree(origin);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_fetch_link_failure)
{
	if (geteuid() == 0) {
		PASS();
	}
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	sandbox_on(root);
	sds tail = sdscatprintf(sdsempty(), "[dependencies]\nlib = { git = \"%s\" }\n", origin);
	proj_enter(root, "linkproj", tail);
	sdsfree(tail);
	mkdir("deps", 0755);
	chmod("deps", 0555);

	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_fetch, "fetch", nullptr, nullptr);
	stderr_end();
	chmod("deps", 0755);
	ASSERT(ret == 1, "fetch fails when deps/ is not writable");
	ASSERT(file_contains("err.txt", "cannot create symlink"), "symlink failure is reported");

	sdsfree(origin);
	sandbox_off();
	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* update                                                               */
/* ------------------------------------------------------------------ */

TEST(cov_update_git_dep_follows_origin)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	sds line   = lib_line(origin);
	sdsfree(origin);
	origin = git_project_enter(root, line);
	sdsfree(line);

	origin_advance(origin, "int later(void) { return 2; }\n");
	sds head = git_head(origin);
	ASSERT(run_cmd(handle_update, "update", nullptr, nullptr) == 0, "update succeeds");
	sds lock = lock_text();
	ASSERT(strstr(lock, head) != nullptr, "lockfile pins the new origin head");
	sdsfree(lock);
	sdsfree(head);

	/* Updating by name behaves the same. */
	origin_advance(origin, "int latest(void) { return 3; }\n");
	head = git_head(origin);
	ASSERT(run_cmd(handle_update, "update", "lib", nullptr) == 0, "targeted update succeeds");
	lock = lock_text();
	ASSERT(strstr(lock, head) != nullptr, "lockfile pins the newest head");
	sdsfree(lock);
	sdsfree(head);

	/* A dead remote is reported but does not abort the command. */
	git_in("deps/lib", "remote", "set-url", "origin", "/nonexistent/coffee-covdeps-remote", nullptr);
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_update, "update", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 0, "update survives a failing git dependency");
	ASSERT(file_contains("err.txt", "Failed to update git dep 'lib'"), "failure is reported");

	sdsfree(origin);
	leave_dir(root);
	PASS();
}

TEST(cov_update_target_preserves_other_entries)
{
	sds root = tmp_new();
	enter_dir(root);
	make_local_dep("libp", "1.0.0", nullptr);
	make_local_dep("libq", "1.0.0", nullptr);
	make_local_dep("libr", "1.0.0", nullptr);
	write_project("targeted", "dependencies = [\"libp\", \"libq\", \"libr\"]", nullptr);

	lockfile_dep_t old_deps[1];
	memset(old_deps, 0, sizeof(old_deps));
	old_deps[0].name    = sdsnew("libq");
	old_deps[0].version = sdsnew("9.9.9");
	old_deps[0].path    = sdsnew("deps/libq");
	old_deps[0].commit  = sdsnew("abc123");
	lockfile_t old_lf;
	memset(&old_lf, 0, sizeof(old_lf));
	old_lf.version    = 1;
	old_lf.deps       = old_deps;
	old_lf.deps_count = 1;
	sds lock_path     = sdsnew("Coffee.lock");
	ASSERT(lockfile_write(lock_path, &old_lf) == 0, "seed lockfile written");
	sdsfree(lock_path);
	sdsfree(old_deps[0].name);
	sdsfree(old_deps[0].version);
	sdsfree(old_deps[0].path);
	sdsfree(old_deps[0].commit);

	ASSERT(run_cmd(handle_update, "update", "libp", nullptr) == 0, "targeted update succeeds");
	sds lock = lock_text();
	ASSERT(strstr(lock, "name = \"libp\"") != nullptr, "target is recorded");
	ASSERT(strstr(lock, "name = \"libq\"") != nullptr, "other locked entry is preserved");
	ASSERT(strstr(lock, "9.9.9") != nullptr, "preserved entry keeps its version");
	ASSERT(strstr(lock, "abc123") != nullptr, "preserved entry keeps its commit");
	ASSERT(strstr(lock, "libr") == nullptr, "unlocked non-target is skipped");
	sdsfree(lock);

	leave_dir(root);
	PASS();
}

TEST(cov_update_failures)
{
	sds root = tmp_new();
	enter_dir(root);

	write_text("Coffee.toml", "[package\nname = \n");
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_update, "update", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unparsable manifest");
	ASSERT(file_contains("err.txt", "Could not parse Coffee.toml"), "parse failure reported");

	write_project("bad name", nullptr, nullptr);
	stderr_begin("err.txt");
	ret = run_cmd(handle_update, "update", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unresolvable graph");
	ASSERT(file_contains("err.txt", "Could not resolve dependency graph"), "graph failure reported");

	/* A directory in the lockfile's place makes the write fail. */
	write_project("lockdir", nullptr, nullptr);
	mkdir("Coffee.lock", 0755);
	stderr_begin("err.txt");
	ret = run_cmd(handle_update, "update", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "lockfile write failure");
	ASSERT(file_contains("err.txt", "Could not write Coffee.lock"), "write failure reported");

	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* outdated                                                             */
/* ------------------------------------------------------------------ */

static void clone_into(const char *origin, const char *dest)
{
	char *argv[] = { "git", "clone", "-q", "--", unconst(origin), unconst(dest), nullptr };
	run_command(argv, RUN_CMD_QUIET);
}

/* Write Coffee.lock with the given (name, version, commit) triples. */
static void write_lock(size_t count, const char *const *names, const char *const *versions, const char *const *commits)
{
	lockfile_t lf;
	memset(&lf, 0, sizeof(lf));
	lf.version    = 1;
	lf.deps       = safe_calloc(count, sizeof(lockfile_dep_t));
	lf.deps_count = count;
	for (size_t i = 0; i < count; i++) {
		lf.deps[i].name    = sdsnew(names[i]);
		lf.deps[i].version = sdsnew(versions[i]);
		lf.deps[i].path    = sdsnew("");
		lf.deps[i].commit  = commits[i] != nullptr ? sdsnew(commits[i]) : nullptr;
	}
	sds path = sdsnew("Coffee.lock");
	lockfile_write(path, &lf);
	sdsfree(path);
	for (size_t i = 0; i < count; i++) {
		sdsfree(lf.deps[i].name);
		sdsfree(lf.deps[i].version);
		sdsfree(lf.deps[i].path);
		sdsfree(lf.deps[i].commit);
	}
	safe_free(lf.deps);
}

TEST(cov_outdated_report_rows)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	proj_enter(root, "outproj", nullptr);
	write_project("outproj", "dependencies = [\"lib\", \"broken\", \"fresh\", \"libn\"]", nullptr);
	mkdir("deps", 0755);
	clone_into(origin, "deps/lib");
	clone_into(origin, "deps/broken");
	make_local_dep("libn", "2.0.0", nullptr);
	sds old_head = git_head(origin);
	origin_advance(origin, "int later(void) { return 2; }\n");
	clone_into(origin, "deps/fresh");
	git_in("deps/broken", "remote", "set-url", "origin", "/nonexistent/coffee-covdeps-remote", nullptr);

	const char *names[]    = { "lib", "broken", "fresh", "libn" };
	const char *versions[] = { "*", "*", "*", "2.0.0" };
	const char *commits[]  = { old_head, "abc", nullptr, nullptr };
	write_lock(4, names, versions, commits);

	stdout_begin("out.txt");
	i64 ret = run_cmd(handle_outdated, "outdated", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 1, "an outdated dependency makes the command fail");
	ASSERT(file_contains("out.txt", "1 commit behind"), "behind status shown");
	ASSERT(file_contains("out.txt", "(fetch failed)"), "fetch failure shown");
	ASSERT(file_contains("out.txt", "Up-to-date"), "up-to-date status shown");
	ASSERT(file_contains("out.txt", "(not a git dep)"), "non-git dependency shown");
	ASSERT(file_contains("out.txt", "1 dependency is outdated."), "summary shown");
	ASSERT(file_contains("out.txt", "abc "), "short commit shown whole");
	char short_sha[13];
	memccpy(short_sha, old_head, '\0', 12);
	short_sha[12] = '\0';
	ASSERT(file_contains("out.txt", short_sha), "long commit truncated");

	/* Only current dependencies: success. */
	const char *names2[]    = { "fresh", "libn" };
	const char *versions2[] = { "*", "2.0.0" };
	const char *commits2[]  = { nullptr, nullptr };
	write_lock(2, names2, versions2, commits2);
	stdout_begin("out.txt");
	ret = run_cmd(handle_outdated, "outdated", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 0, "all current succeeds");
	ASSERT(file_contains("out.txt", "All dependencies are up-to-date."), "summary shown");

	sdsfree(old_head);
	sdsfree(origin);
	leave_dir(root);
	PASS();
}

TEST(cov_outdated_failures)
{
	sds root = tmp_new();
	enter_dir(root);

	write_text("Coffee.toml", "[package\nname = \n");
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_outdated, "outdated", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unparsable manifest");
	ASSERT(file_contains("err.txt", "Could not parse Coffee.toml"), "parse failure reported");

	write_project("bad name", nullptr, nullptr);
	const char *names[]    = { "x" };
	const char *versions[] = { "*" };
	const char *commits[]  = { nullptr };
	write_lock(1, names, versions, commits);
	stderr_begin("err.txt");
	ret = run_cmd(handle_outdated, "outdated", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unresolvable graph");
	ASSERT(file_contains("err.txt", "Could not resolve dependency graph"), "graph failure reported");

	leave_dir(root);
	PASS();
}

TEST(cov_outdated_empty_lockfile)
{
	sds root = tmp_new();
	enter_dir(root);

	write_project("emptylock", nullptr, nullptr);
	write_lock(0, nullptr, nullptr, nullptr);
	stdout_begin("out.txt");
	i64 ret = run_cmd(handle_outdated, "outdated", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 0, "an empty lockfile is not an error");
	ASSERT(file_contains("out.txt", "No dependencies in lockfile."), "empty lockfile reported");

	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* generate-lockfile                                                    */
/* ------------------------------------------------------------------ */

TEST(cov_genlock_records_git_commit)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	sds line   = lib_line(origin);
	sdsfree(origin);
	origin = git_project_enter(root, line);
	sdsfree(line);

	ASSERT(run_cmd(handle_generate_lockfile, "generate-lockfile", nullptr, nullptr) == 0, "generation succeeds");
	sds head = git_head("deps/lib");
	sds lock = lock_text();
	ASSERT(strstr(lock, "name = \"lib\"") != nullptr, "dependency listed");
	ASSERT(strstr(lock, head) != nullptr, "git commit pinned");
	sdsfree(lock);
	sdsfree(head);

	sdsfree(origin);
	leave_dir(root);
	PASS();
}

TEST(cov_genlock_failures)
{
	sds root = tmp_new();
	enter_dir(root);

	write_text("Coffee.toml", "[package\nname = \n");
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_generate_lockfile, "generate-lockfile", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unparsable manifest");
	ASSERT(file_contains("err.txt", "Could not parse Coffee.toml"), "parse failure reported");

	write_project("bad name", nullptr, nullptr);
	stderr_begin("err.txt");
	ret = run_cmd(handle_generate_lockfile, "generate-lockfile", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unresolvable graph");
	ASSERT(file_contains("err.txt", "Could not resolve dependency graph"), "graph failure reported");

	write_project("lockdir", nullptr, nullptr);
	mkdir("Coffee.lock", 0755);
	stderr_begin("err.txt");
	ret = run_cmd(handle_generate_lockfile, "generate-lockfile", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "lockfile write failure");
	ASSERT(file_contains("err.txt", "Could not write Coffee.lock"), "write failure reported");

	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* vendor                                                               */
/* ------------------------------------------------------------------ */

TEST(cov_vendor_structured_sources)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	sds tail = sdscatprintf(sdsempty(),
	                        "[dependencies]\n"
	                        "gitdep = { git = \"%s\" }\n"
	                        "httpdep = { git = \"http://example.com/x.git\" }\n"
	                        "deadgit = { git = \"/nonexistent/coffee-covdeps-repo\" }\n"
	                        "pathdep = { path = \"./local/pathdep\" }\n"
	                        "deadpath = { path = \"./local/absent\" }\n"
	                        "bare = { version = \"1.0\" }\n",
	                        origin);
	proj_enter(root, "vendorproj", tail);
	sdsfree(tail);
	mkdir("local", 0755);
	mkdir("local/pathdep", 0755);
	write_text("local/pathdep/library.toml", "[package]\nname = \"pathdep\"\nversion = \"1.0.0\"\n");

	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_vendor, "vendor", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 0, "vendor completes");
	ASSERT(safe_access("vendor/gitdep/.git", F_OK) == 0, "git dependency vendored");
	ASSERT(safe_access("vendor/pathdep/library.toml", F_OK) == 0, "path dependency vendored");
	ASSERT(file_contains("err.txt", "invalid git URL for httpdep"), "http URL refused");
	ASSERT(file_contains("err.txt", "Failed to vendor git dep deadgit"), "git failure reported");
	ASSERT(file_contains("err.txt", "Failed to vendor path dep deadpath"), "path failure reported");
	ASSERT(file_contains("err.txt", "bare has no git or path source"), "sourceless dependency reported");

	sdsfree(origin);
	leave_dir(root);
	PASS();
}

TEST(cov_vendor_flat_entries)
{
	sds root = tmp_new();
	enter_dir(root);
	write_project("flatvendor", "dependencies = [\"bad name\", \"versioned = \\\"1.0\\\"\"]", nullptr);

	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_vendor, "vendor", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 0, "vendor completes");
	ASSERT(file_contains("err.txt", "skipping invalid dependency name 'bad name'"), "invalid name skipped");
	ASSERT(file_contains("err.txt", "versioned has no structured dep info"), "flat entry reported");

	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* install                                                              */
/* ------------------------------------------------------------------ */

/* A package repository; manifest_text becomes library.toml (nullptr: none). */
static sds package_repo(const char *root, const char *manifest_text, bool with_main)
{
	sds repo = join(root, "pkg");
	mkdir(repo, 0755);
	sds src = join(repo, "src");
	mkdir(src, 0755);
	sdsfree(src);
	sds main_c = join(repo, "src/hello.c");
	write_text(main_c, with_main ? "int main(void) { return 0; }\n" : "int hello_lib(void) { return 1; }\n");
	sdsfree(main_c);
	if (manifest_text != nullptr) {
		sds toml = join(repo, "library.toml");
		write_text(toml, manifest_text);
		sdsfree(toml);
	}
	git_in(repo, "init", "-q", "-b", "main", nullptr);
	git_in(repo, "add", "-A", nullptr, nullptr, nullptr);
	git_in(repo, "commit", "-q", "-m", "pkg", nullptr);
	return repo;
}

static i64 install_pkg(const char *repo, const char *name, const char *version, bool verbose)
{
	options opt;
	memset(&opt, 0, sizeof(opt));
	char *inputs[] = { "install", unconst(name), unconst(version), nullptr };
	opt.inputs     = inputs;
	opt.inputs_num = version != nullptr ? 3 : 2;
	opt.git        = sdsnew(repo);
	opt.verbose    = verbose;
	i64 ret        = handle_install(&opt);
	sdsfree(opt.git);
	return ret;
}

TEST(cov_install_builds_binaries)
{
	sds root = tmp_new();
	sandbox_on(root);
	sds repo = package_repo(root,
	                        "dependencies = [\"helper\"]\n"
	                        "[package]\nname = \"tool\"\nversion = \"1.2.3\"\n\n"
	                        "[[bin]]\nname = \"hello\"\n\n"
	                        "[[bin]]\nname = \"bad/name\"\n\n"
	                        "[[bin]]\nname = \"broken\"\n\n"
	                        "[[bin]]\nsrc = [\"src/hello.c\"]\n",
	                        true);
	proj_enter(root, "installproj", nullptr);
	make_local_dep("helper", "1.0.0", nullptr);
	fake_cc_on(root);

	stdout_begin("out.txt");
	stderr_begin("err.txt");
	i64 ret = install_pkg(repo, "tool", "1.2.3", true);
	stderr_end();
	stdout_end();
	fake_cc_off();
	ASSERT(ret == 0, "install succeeds");
	sds home   = sdsnew(getenv("COFFEE_HOME"));
	sds cached = join(home, "deps/tool/1.2.3");
	ASSERT(safe_access(cached, F_OK) == 0, "versioned directory created");
	sdsfree(cached);
	sdsfree(home);
	ASSERT(file_contains("out.txt", "Binary:"), "binary reported");
	ASSERT(file_contains("out.txt", "Installing package: tool (version 1.2.3)"), "requested version reported");
	ASSERT(file_contains("err.txt", "invalid binary target name 'bad/name'"), "bad target name refused");
	ASSERT(file_contains("err.txt", "Failed to compile binary 'broken'"), "compile failure reported");
	ASSERT(safe_access("deps/tool", F_OK) == 0, "project link created");

	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_install_dep_flag_fallback)
{
	sds root = tmp_new();
	sandbox_on(root);
	/* An invalid package name makes the graph unusable: direct deps are resolved by hand. */
	sds repo = package_repo(root,
	                        "dependencies = [\"helper\", \"missing\"]\n"
	                        "[package]\nname = \"bad name\"\nversion = \"1.0.0\"\n\n"
	                        "[[bin]]\nname = \"hello\"\n",
	                        true);
	proj_enter(root, "fallbackproj", nullptr);
	make_local_dep("helper", "1.0.0", nullptr);
	mkdir("deps/helper/include", 0755);
	fake_cc_on(root);

	stdout_begin("out.txt");
	i64 ret = install_pkg(repo, "tool", nullptr, false);
	stdout_end();
	fake_cc_off();
	ASSERT(ret == 0, "install succeeds");
	ASSERT(file_contains("out.txt", "Binary:"), "binary reported");

	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_install_source_only_and_version_warning)
{
	sds root = tmp_new();
	sandbox_on(root);
	sds repo = package_repo(root, nullptr, false);
	proj_enter(root, "srcproj", nullptr);

	stdout_begin("out.txt");
	i64 ret = install_pkg(repo, "tool", nullptr, false);
	stdout_end();
	ASSERT(ret == 0, "install without library.toml succeeds");
	ASSERT(file_contains("out.txt", "Source installed to"), "source-only install reported");
	sds home = sdsnew(getenv("COFFEE_HOME"));
	sds dir  = join(home, "deps/tool/*");
	ASSERT(safe_access(dir, F_OK) == 0, "wildcard version directory created");
	sdsfree(dir);
	sdsfree(home);
	sdsfree(repo);

	/* A version that does not match what was cloned only warns. */
	test_remove_tree("pkg");
	test_remove_tree(root);
	mkdir(root, 0755);
	sandbox_off();
	sandbox_on(root);
	repo = package_repo(root, "[package]\nname = \"tool\"\nversion = \"1.0.0\"\n", false);
	proj_enter(root, "warnproj", nullptr);
	stderr_begin("err.txt");
	ret = install_pkg(repo, "tool", "2.0.0", false);
	stderr_end();
	ASSERT(ret == 0, "mismatching version still installs");
	ASSERT(file_contains("err.txt", "requested version 2.0.0 but cloned version is 1.0.0"), "warning shown");

	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_install_rejects_bad_input)
{
	sds root = tmp_new();
	sandbox_on(root);
	enter_dir(root);

	stderr_begin("err.txt");
	i64 ret = install_pkg("/nonexistent/coffee-covdeps-pkg", "bad name", nullptr, false);
	stderr_end();
	ASSERT(ret == 1, "invalid package name");
	ASSERT(file_contains("err.txt", "invalid package name 'bad name'"), "name refusal reported");

	stderr_begin("err.txt");
	ret = install_pkg("http://example.com/x.git", "tool", nullptr, false);
	stderr_end();
	ASSERT(ret == 1, "invalid URL");
	ASSERT(file_contains("err.txt", "invalid --git URL"), "URL refusal reported");

	stderr_begin("err.txt");
	ret = install_pkg("/nonexistent/coffee-covdeps-pkg", "tool", nullptr, false);
	stderr_end();
	ASSERT(ret == 1, "clone failure");
	ASSERT(file_contains("err.txt", "Failed to clone tool"), "clone failure reported");

	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_install_symlink_failures)
{
	if (geteuid() == 0) {
		PASS();
	}
	sds root = tmp_new();
	sandbox_on(root);
	sds repo = package_repo(root, "[package]\nname = \"tool\"\nversion = \"1.0.0\"\n", false);
	proj_enter(root, "linkproj", nullptr);

	/* deps is a file: the parent directory of the link cannot be created. */
	write_text("deps", "not a directory\n");
	stderr_begin("err.txt");
	i64 ret = install_pkg(repo, "tool", nullptr, false);
	stderr_end();
	ASSERT(ret == 0, "install still succeeds");
	ASSERT(file_contains("err.txt", "Failed to create project symlink"), "mkdir failure reported");
	remove("deps");

	/* deps is read-only: the link itself cannot be created. */
	mkdir("deps", 0755);
	chmod("deps", 0555);
	stderr_begin("err.txt");
	ret = install_pkg(repo, "tool", nullptr, false);
	stderr_end();
	ASSERT(ret == 0, "install still succeeds");
	ASSERT(file_contains("err.txt", "Failed to create project symlink"), "symlink failure reported");

	/* A stale regular file cannot be unlinked from a read-only directory. */
	chmod("deps", 0755);
	write_text("deps/tool", "stale\n");
	chmod("deps", 0555);
	stderr_begin("err.txt");
	ret = install_pkg(repo, "tool", nullptr, false);
	stderr_end();
	chmod("deps", 0755);
	ASSERT(ret == 0, "install still succeeds");
	ASSERT(file_contains("err.txt", "Failed to create project symlink"), "unlink failure reported");
	remove("deps/tool");

	/* An existing link that cannot be removed. */
	sds home   = sdsnew(getenv("COFFEE_HOME"));
	sds target = join(home, "deps/tool/1.0.0");
	sds clone  = join(home, "deps/tool");
	test_remove_tree(clone);
	sdsfree(clone);
	ASSERT(symlink(target, "deps/tool") == 0, "stale link created");
	sdsfree(target);
	sdsfree(home);
	stub_rm_on(root);
	stderr_begin("err.txt");
	ret = install_pkg(repo, "tool", nullptr, false);
	stderr_end();
	stub_off();
	ASSERT(ret == 0, "install still succeeds");
	ASSERT(file_contains("err.txt", "Failed to create project symlink"), "rm failure reported");

	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

/* Point CC at a fake compiler that appends its arguments to root/cc.log. */
static void recording_cc_on(const char *root)
{
	sds script = join(root, "recordcc");
	sds log    = join(root, "cc.log");
	sds text   = sdscatprintf(sdsempty(), "#!/bin/sh\necho \"$*\" >> %s\nexit 0\n", log);
	write_text(script, text);
	chmod(script, 0755);
	saved_cc = env_save("CC");
	setenv("CC", script, 1);
	sdsfree(text);
	sdsfree(log);
	sdsfree(script);
}

static bool is_symlink_to(const char *path, const char *target)
{
	char    buf[4'096];
	ssize_t len = readlink(path, buf, sizeof(buf) - 1);
	if (len < 0) {
		return false;
	}
	buf[len] = '\0';
	return strcmp(buf, target) == 0;
}

/* Regression: compile_binary used to free the manifest-owned bt->src entries (double free). */
TEST(cov_install_bin_with_sources_and_layout)
{
	sds root = tmp_new();
	sandbox_on(root);
	sds repo = package_repo(root,
	                        "[package]\nname = \"tool\"\nversion = \"1.0.0\"\n\n"
	                        "[[bin]]\nname = \"hello\"\nsrc = [\"src/hello.c\", \"src/extra.c\"]\n\n"
	                        "[[bin]]\nname = \"second\"\nsrc = [\"src/hello.c\"]\n",
	                        true);
	proj_enter(root, "binproj", nullptr);
	recording_cc_on(root);
	sds home   = sdsnew(getenv("COFFEE_HOME"));
	sds clone  = join(home, "deps/.tool.clone");
	sds cached = join(home, "deps/tool/1.0.0");

	/* A leftover temporary clone from an interrupted install is discarded. */
	mkdir(home, 0755);
	sds deps = join(home, "deps");
	mkdir(deps, 0755);
	sdsfree(deps);
	mkdir(clone, 0755);
	sds junk = join(clone, "junk.txt");
	write_text(junk, "junk\n");
	sdsfree(junk);

	stdout_begin("out.txt");
	i64 ret = install_pkg(repo, "tool", nullptr, false);
	stdout_end();
	fake_cc_off();
	ASSERT(ret == 0, "install with binary sources succeeds");

	sds log      = join(root, "cc.log");
	sds expected = sdscatprintf(sdsempty(), "-o %s/bin/hello src/hello.c src/extra.c", home);
	ASSERT(file_contains(log, expected), "first binary: output path and all sources passed");
	sdsfree(expected);
	expected = sdscatprintf(sdsempty(), "-o %s/bin/second src/hello.c", home);
	ASSERT(file_contains(log, expected), "second binary: output path and source passed");
	sdsfree(expected);
	sdsfree(log);
	ASSERT(file_contains("out.txt", "Binary:"), "binary reported");

	/* The clone was moved into the versioned directory. */
	sds lib = join(cached, "library.toml");
	ASSERT(safe_access(lib, F_OK) == 0, "library.toml in the versioned directory");
	sdsfree(lib);
	sds hello = join(cached, "src/hello.c");
	ASSERT(safe_access(hello, F_OK) == 0, "sources in the versioned directory");
	sdsfree(hello);
	sds git_dir = join(cached, ".git");
	ASSERT(safe_access(git_dir, F_OK) == 0, "repository moved as a whole");
	sdsfree(git_dir);
	ASSERT(safe_access(clone, F_OK) != 0, "temporary clone directory is gone");

	/* The project link points to the versioned directory, which has the sources. */
	ASSERT(is_symlink_to("deps/tool", cached), "project link targets the versioned directory");
	ASSERT(safe_access("deps/tool/library.toml", F_OK) == 0, "manifest reachable through the link");
	ASSERT(safe_access("deps/tool/src/hello.c", F_OK) == 0, "sources reachable through the link");

	/* Reinstalling replaces the previous contents. */
	sds stale = join(cached, "stale.txt");
	write_text(stale, "old\n");
	ASSERT(safe_access(stale, F_OK) == 0, "stale file planted");
	recording_cc_on(root);
	stdout_begin("out.txt");
	ret = install_pkg(repo, "tool", nullptr, false);
	stdout_end();
	fake_cc_off();
	ASSERT(ret == 0, "reinstall succeeds");
	ASSERT(safe_access(stale, F_OK) != 0, "previous contents replaced");
	ASSERT(safe_access("deps/tool/library.toml", F_OK) == 0, "link still resolves");
	ASSERT(safe_access(clone, F_OK) != 0, "temporary clone directory is gone after reinstall");
	ASSERT(is_symlink_to("deps/tool", cached), "link still targets the versioned directory");
	sdsfree(stale);

	sdsfree(cached);
	sdsfree(clone);
	sdsfree(home);
	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_install_rejects_unsafe_version)
{
	sds root = tmp_new();
	sandbox_on(root);
	sds repo = package_repo(root, "[package]\nname = \"tool\"\nversion = \"../evil\"\n", false);
	proj_enter(root, "evilproj", nullptr);

	stderr_begin("err.txt");
	i64 ret = install_pkg(repo, "tool", nullptr, false);
	stderr_end();
	ASSERT(ret == 1, "unsafe version refused");
	ASSERT(file_contains("err.txt", "invalid version '../evil'"), "refusal reported");
	sds home  = sdsnew(getenv("COFFEE_HOME"));
	sds clone = join(home, "deps/.tool.clone");
	sds pkg   = join(home, "deps/tool");
	ASSERT(safe_access(clone, F_OK) != 0, "temporary clone removed");
	ASSERT(safe_access(pkg, F_OK) != 0, "nothing installed");
	ASSERT(safe_access("deps/tool", F_OK) != 0, "no project link");
	sdsfree(pkg);
	sdsfree(clone);
	sdsfree(home);

	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_install_rename_fallback_moves_with_mv)
{
	sds root = tmp_new();
	sandbox_on(root);
	sds repo = package_repo(root, "[package]\nname = \"tool\"\nversion = \"1.0.0\"\n", false);
	proj_enter(root, "mvproj", nullptr);

	/* rm fails, so the versioned directory stays non-empty and rename() refuses to replace it. */
	sds home   = sdsnew(getenv("COFFEE_HOME"));
	sds deps   = join(home, "deps");
	sds pkg    = join(deps, "tool");
	sds cached = join(pkg, "1.0.0");
	mkdir(deps, 0755);
	mkdir(pkg, 0755);
	mkdir(cached, 0755);
	sds old = join(cached, "old.txt");
	write_text(old, "old\n");
	stub_rm_on(root);
	stdout_begin("out.txt");
	i64 ret = install_pkg(repo, "tool", nullptr, false);
	stdout_end();
	stub_off();
	ASSERT(ret == 0, "install falls back to mv");
	ASSERT(file_contains("out.txt", "Installed: tool"), "install reported");
	sds moved = join(cached, ".tool.clone/library.toml");
	ASSERT(safe_access(moved, F_OK) == 0, "mv moved the clone into the versioned directory");

	sdsfree(moved);
	sdsfree(old);
	sdsfree(cached);
	sdsfree(pkg);
	sdsfree(deps);
	sdsfree(home);
	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* install-update and install-update-config                             */
/* ------------------------------------------------------------------ */

TEST(cov_install_update_paths)
{
	sds root   = tmp_new();
	sds origin = join(root, "origin");
	origin_make(origin);
	sandbox_on(root);
	enter_dir(root);
	sds home = sdsnew(getenv("COFFEE_HOME"));

	/* No deps directory at all. */
	stdout_begin("out.txt");
	i64 ret = run_cmd(handle_install_update, "install-update", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 0, "nothing installed");
	ASSERT(file_contains("out.txt", "No packages to update."), "empty home reported");

	/* An empty deps directory. */
	sds deps = join(home, "deps");
	mkdir(deps, 0755);
	stdout_begin("out.txt");
	ret = run_cmd(handle_install_update, "install-update", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 0, "empty deps directory");
	ASSERT(file_contains("out.txt", "No packages to update."), "empty deps reported");

	/* Two packages: one updatable clone and one directory that is not a repository. */
	sds pkg = join(deps, "good");
	clone_into(origin, pkg);
	sds plain = join(deps, "plain");
	mkdir(plain, 0755);
	origin_advance(origin, "int later(void) { return 2; }\n");
	stdout_begin("out.txt");
	stderr_begin("err.txt");
	ret = run_cmd(handle_install_update, "install-update", nullptr, nullptr);
	stderr_end();
	stdout_end();
	ASSERT(ret == 0, "update of all packages");
	ASSERT(file_contains("out.txt", "Updated: good"), "good package updated");
	ASSERT(file_contains("err.txt", "Failed to update plain"), "plain directory reported");
	sds head_origin = git_head(origin);
	sds head_clone  = git_head(pkg);
	ASSERT(strcmp(head_origin, head_clone) == 0, "clone fast-forwarded");
	sdsfree(head_origin);
	sdsfree(head_clone);

	/* A named package. */
	origin_advance(origin, "int latest(void) { return 3; }\n");
	stdout_begin("out.txt");
	ret = run_cmd(handle_install_update, "install-update", "good", nullptr);
	stdout_end();
	ASSERT(ret == 0, "named update");
	ASSERT(file_contains("out.txt", "Updated: good"), "named package updated");
	stderr_begin("err.txt");
	ret = run_cmd(handle_install_update, "install-update", "plain", nullptr);
	stderr_end();
	ASSERT(ret == 1, "named update of a non-repository fails");
	ASSERT(file_contains("err.txt", "Failed to update plain"), "failure reported");
	stderr_begin("err.txt");
	ret = run_cmd(handle_install_update, "install-update", "absent", nullptr);
	stderr_end();
	ASSERT(ret == 1, "unknown package");
	ASSERT(file_contains("err.txt", "'absent' is not installed"), "unknown package reported");

	/* deps is a file: it exists but cannot be listed. */
	test_remove_tree(deps);
	write_text(deps, "file\n");
	stdout_begin("out.txt");
	ret = run_cmd(handle_install_update, "install-update", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 0, "unlistable deps");
	ASSERT(file_contains("out.txt", "No packages to update."), "unlistable deps reported");

	sdsfree(plain);
	sdsfree(pkg);
	sdsfree(deps);
	sdsfree(home);
	sdsfree(origin);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_install_update_config_merges_missing_keys)
{
	sds root = tmp_new();
	sandbox_on(root);
	enter_dir(root);
	sds home = sdsnew(getenv("COFFEE_HOME"));
	sds cfg  = join(home, "config.toml");
	write_text(cfg, "default-target = \"release\"\n"
	                "default-edition = \"c11\"\n"
	                "[registry]\n"
	                "mirror = \"x\"\n"
	                "[other]\n"
	                "value = 1\n"
	                "[build]\n"
	                "threads = 4\n");

	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	sds text = read_text(cfg);
	ASSERT(strstr(text, "default-target = \"release\"") != nullptr, "existing value kept");
	ASSERT(strstr(text, "default-edition = \"c11\"") != nullptr, "existing value kept");
	ASSERT(strstr(text, "jobs = \"0\"") != nullptr, "key added to an existing section");
	const char *registry = strstr(text, "[registry]");
	const char *index    = strstr(text, "index-url = ");
	const char *other    = strstr(text, "[other]");
	const char *jobs     = strstr(text, "jobs = ");
	const char *build    = strstr(text, "[build]");
	ASSERT(registry != nullptr && index != nullptr && other != nullptr && registry < index && index < other,
	       "registry key lands inside its section");
	ASSERT(build != nullptr && jobs != nullptr && build < jobs, "build key lands inside its section");
	sdsfree(text);

	/* Everything present now. */
	stdout_begin("out.txt");
	i64 ret = run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 0, "second run succeeds");
	ASSERT(file_contains("out.txt", "up-to-date"), "nothing to add");

	/* Missing sections are created at the end of the file. */
	write_text(cfg, "default-target = \"x\"\ndefault-edition = \"c23\"\n");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	text = read_text(cfg);
	ASSERT(strstr(text, "[build]\njobs = \"0\"") != nullptr, "build section created");
	ASSERT(strstr(text, "[registry]\nindex-url = ") != nullptr, "registry section created");
	sdsfree(text);

	/* An unparsable file: every default is appended. */
	write_text(cfg, "[broken\n");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	text = read_text(cfg);
	ASSERT(strstr(text, "default-target") != nullptr, "defaults appended to a broken file");
	sdsfree(text);

	/* A directory in the config's place cannot be written. */
	test_remove_tree(cfg);
	mkdir(cfg, 0755);
	stderr_begin("err.txt");
	ret = run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "unwritable config");
	ASSERT(file_contains("err.txt", "Could not write"), "write failure reported");

	sdsfree(cfg);
	sdsfree(home);
	sandbox_off();
	leave_dir(root);
	PASS();
}

static toml_table_t *parse_config(const char *path, char *errbuf, size_t errlen)
{
	FILE *fp = fopen(path, "r");
	if (fp == nullptr) {
		return nullptr;
	}
	toml_table_t *conf = toml_parse_file(fp, errbuf, (int)errlen);
	fclose(fp);
	return conf;
}

/* Assert that both default top-level keys parse at the top level of the file. */
static bool config_has_top_level_defaults(const char *path)
{
	char          err[256];
	toml_table_t *conf = parse_config(path, err, sizeof(err));
	if (conf == nullptr) {
		return false;
	}
	bool ok = toml_raw_in(conf, "default-target") != nullptr && toml_raw_in(conf, "default-edition") != nullptr;
	toml_free(conf);
	return ok;
}

/* Regression: a missing top-level key used to be appended inside the last section. */
TEST(cov_install_update_config_top_level_keys_precede_sections)
{
	sds root = tmp_new();
	sandbox_on(root);
	enter_dir(root);
	sds home = sdsnew(getenv("COFFEE_HOME"));
	sds cfg  = join(home, "config.toml");

	/* Only a section: the top-level keys must come before it. */
	mkdir(home, 0755);
	write_text(cfg, "# my config\n\n[build]\njobs = \"4\"\n");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	sds         text   = read_text(cfg);
	const char *target = strstr(text, "default-target = ");
	const char *edit   = strstr(text, "default-edition = ");
	const char *build  = strstr(text, "[build]");
	ASSERT(target != nullptr && edit != nullptr && build != nullptr, "keys and section present");
	ASSERT(target < build && edit < build, "top-level keys precede the first section");
	ASSERT(strstr(text, "# my config\n") == text, "leading comment preserved");
	ASSERT(config_has_top_level_defaults(cfg), "keys parse as top-level");
	char          err[256];
	toml_table_t *conf = parse_config(cfg, err, sizeof(err));
	ASSERT(conf != nullptr, "file parses");
	toml_table_t *tab = toml_table_in(conf, "build");
	ASSERT(tab != nullptr && toml_raw_in(tab, "default-target") == nullptr, "build section not polluted");
	ASSERT(tab != nullptr && toml_raw_in(tab, "default-edition") == nullptr, "build section not polluted");
	toml_raw_t jobs = tab != nullptr ? toml_raw_in(tab, "jobs") : nullptr;
	ASSERT(jobs != nullptr && strcmp(jobs, "\"4\"") == 0, "existing value kept");
	ASSERT(toml_table_in(conf, "registry") != nullptr, "registry section created");
	toml_free(conf);

	/* A second run changes nothing. */
	sds first = text;
	stdout_begin("out.txt");
	i64 ret = run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr);
	stdout_end();
	ASSERT(ret == 0, "second run succeeds");
	ASSERT(file_contains("out.txt", "up-to-date"), "reports up-to-date");
	text = read_text(cfg);
	ASSERT(strcmp(first, text) == 0, "file unchanged");
	sdsfree(text);
	sdsfree(first);

	/* Top-level keys but no section: the missing key and the sections are added. */
	write_text(cfg, "default-target = \"release\"\n");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	text = read_text(cfg);
	ASSERT(strstr(text, "default-target = \"release\"\n") == text, "existing key stays first");
	ASSERT(strstr(text, "default-edition = \"c23\"") != nullptr, "missing key added");
	ASSERT(config_has_top_level_defaults(cfg), "keys parse as top-level");
	sdsfree(text);

	/* An empty file. */
	write_text(cfg, "");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	ASSERT(config_has_top_level_defaults(cfg), "defaults created in an empty file");
	ASSERT(file_contains(cfg, "[build]\njobs = \"0\""), "sections created in an empty file");

	/* No file at all. */
	remove(cfg);
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	ASSERT(config_has_top_level_defaults(cfg), "defaults created without a file");
	conf = parse_config(cfg, err, sizeof(err));
	ASSERT(conf != nullptr, "created file parses");
	tab = toml_table_in(conf, "registry");
	ASSERT(tab != nullptr && toml_raw_in(tab, "index-url") != nullptr, "registry key in its section");
	toml_free(conf);

	/* A last line without a newline. */
	write_text(cfg, "[build]\njobs = \"2\"");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	ASSERT(config_has_top_level_defaults(cfg), "keys parse as top-level after an unterminated last line");

	/* A top-level last line without a newline must not swallow the added key. */
	write_text(cfg, "default-target = \"x\"");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	ASSERT(config_has_top_level_defaults(cfg), "added key not glued to an unterminated top-level line");

	/* Nested arrays, quoted brackets and comments in a multi-line array are not headers. */
	write_text(cfg, "list = [\n  [1, 2],\n  [3],\n]\nquoted = [\n  \"]\",\n  \"[\", # ] [\n]\n[build]\njobs = \"2\"\n");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	text = read_text(cfg);
	ASSERT(strstr(text, "default-target") < strstr(text, "[build]"), "keys inserted before the real header");
	ASSERT(strstr(text, "default-target") > strstr(text, "quoted"), "keys not inserted inside the array");
	ASSERT(config_has_top_level_defaults(cfg), "keys parse as top-level next to arrays");
	sdsfree(text);

	/* A stray closing bracket (invalid TOML) does not derail header detection. */
	write_text(cfg, "]\n[build]\njobs = \"2\"\n");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	text = read_text(cfg);
	ASSERT(strstr(text, "default-target") < strstr(text, "[build]"), "keys still precede the header");
	sdsfree(text);

	/* An indented section header still ends the top-level part. */
	write_text(cfg, "  [build]\njobs = \"2\"\n");
	ASSERT(run_cmd(handle_install_update_config, "install-update-config", nullptr, nullptr) == 0, "merge succeeds");
	ASSERT(config_has_top_level_defaults(cfg), "keys precede an indented section header");

	sdsfree(cfg);
	sdsfree(home);
	sandbox_off();
	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* add / remove / uninstall                                            */
/* ------------------------------------------------------------------ */

TEST(cov_add_structured_duplicate_and_flags)
{
	sds root = tmp_new();
	enter_dir(root);
	write_text("Coffee.toml", "[package]\nname = \"addproj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n\n"
	                          "[[dependencies]]\nname = \"known\"\ngit = \"https://example.com/k.git\"\n");

	stdout_begin("out.txt");
	i64 ret = run_cmd(handle_add, "add", "known", nullptr);
	stdout_end();
	ASSERT(ret == 0, "re-adding is a no-op");
	ASSERT(file_contains("out.txt", "Dependency known already exists"), "duplicate reported");

	/* --path with --pkg-version, as a dev dependency. */
	write_text("Makefile", "all:\n");
	options opt;
	memset(&opt, 0, sizeof(opt));
	char *inputs[]  = { "add", "pathdep", nullptr };
	opt.inputs      = inputs;
	opt.inputs_num  = 2;
	opt.path        = sdsnew("./vend/pathdep");
	opt.pkg_version = sdsnew("2.1.0");
	opt.dev         = true;
	ret             = handle_add(&opt);
	ASSERT(ret == 0, "path dependency added");
	ASSERT(file_contains("Coffee.toml", "path = \"./vend/pathdep\""), "path written");
	ASSERT(file_contains("Coffee.toml", "version = \"2.1.0\""), "version written");
	ASSERT(file_contains("Makefile", "# Dep: pathdep"), "Makefile extended");

	/* --git as a build dependency. */
	sdsfree(opt.path);
	opt.path        = nullptr;
	char *inputs2[] = { "add", "gitdep", nullptr };
	opt.inputs      = inputs2;
	opt.git         = sdsnew("https://example.com/g.git");
	opt.dev         = false;
	opt.build_dep   = true;
	ret             = handle_add(&opt);
	ASSERT(ret == 0, "git dependency added");
	ASSERT(file_contains("Coffee.toml", "gitdep = { git = \"https://example.com/g.git\" }"), "git written");
	sdsfree(opt.git);
	sdsfree(opt.pkg_version);

	leave_dir(root);
	PASS();
}

TEST(cov_add_manifest_failures)
{
	if (geteuid() == 0) {
		PASS();
	}
	sds root = tmp_new();
	enter_dir(root);

	write_text("Coffee.toml", "[package\nname = \n");
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_add, "add", "newdep", nullptr);
	stderr_end();
	ASSERT(ret == 1, "unparsable manifest");
	ASSERT(file_contains("err.txt", "Could not parse manifest"), "parse failure reported");

	write_project("addfail", nullptr, nullptr);
	options opt;
	memset(&opt, 0, sizeof(opt));
	char *inputs[] = { "add", "newdep", nullptr };
	opt.inputs     = inputs;
	opt.inputs_num = 2;
	opt.git        = sdsnew("https://example.com/n.git");
	chmod(".", 0555);
	stderr_begin("err.txt");
	ret = handle_add(&opt);
	stderr_end();
	chmod(".", 0755);
	sdsfree(opt.git);
	ASSERT(ret == 1, "unwritable manifest directory");
	ASSERT(file_contains("err.txt", "Could not write manifest"), "write failure reported");

	leave_dir(root);
	PASS();
}

TEST(cov_remove_failures)
{
	if (geteuid() == 0) {
		PASS();
	}
	sds root = tmp_new();
	enter_dir(root);

	write_text("Coffee.toml", "[package\nname = \n");
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_remove, "remove", "dep", nullptr);
	stderr_end();
	ASSERT(ret == 1, "unparsable manifest");
	ASSERT(file_contains("err.txt", "Could not parse manifest"), "parse failure reported");

	write_project("rmfail", nullptr, "[dependencies]\ndep = \"1.0\"\n");
	chmod(".", 0555);
	stderr_begin("err.txt");
	ret = run_cmd(handle_remove, "remove", "dep", nullptr);
	stderr_end();
	chmod(".", 0755);
	ASSERT(ret == 1, "unwritable manifest directory");
	ASSERT(file_contains("err.txt", "Could not write manifest"), "write failure reported");

	leave_dir(root);
	PASS();
}

TEST(cov_uninstall_removal_failure)
{
	sds root = tmp_new();
	sandbox_on(root);
	enter_dir(root);
	sds home = sdsnew(getenv("COFFEE_HOME"));
	sds deps = join(home, "deps");
	mkdir(deps, 0755);
	sds pkg = join(deps, "stuck");
	mkdir(pkg, 0755);

	stub_rm_on(root);
	stderr_begin("err.txt");
	i64 ret = run_cmd(handle_uninstall, "uninstall", "stuck", nullptr);
	stderr_end();
	stub_off();
	ASSERT(ret == 1, "failing rm fails the command");
	ASSERT(file_contains("err.txt", "Failed to remove stuck"), "failure reported");

	sdsfree(pkg);
	sdsfree(deps);
	sdsfree(home);
	sandbox_off();
	leave_dir(root);
	PASS();
}

TEST(cov_uninstall_removes_installed_package)
{
	sds root = tmp_new();
	sandbox_on(root);
	sds repo = package_repo(root, "[package]\nname = \"tool\"\nversion = \"1.0.0\"\n", false);
	proj_enter(root, "unproj", nullptr);
	ASSERT(install_pkg(repo, "tool", nullptr, false) == 0, "install succeeds");
	sds home = sdsnew(getenv("COFFEE_HOME"));
	sds pkg  = join(home, "deps/tool");
	sds lib  = join(pkg, "1.0.0/library.toml");
	ASSERT(safe_access(lib, F_OK) == 0, "package installed");

	stdout_begin("out.txt");
	i64 ret = run_cmd(handle_uninstall, "uninstall", "tool", nullptr);
	stdout_end();
	ASSERT(ret == 0, "uninstall succeeds");
	ASSERT(file_contains("out.txt", "Removed: tool"), "removal reported");
	ASSERT(safe_access(pkg, F_OK) != 0, "package directory is gone");

	stderr_begin("err.txt");
	ret = run_cmd(handle_uninstall, "uninstall", "tool", nullptr);
	stderr_end();
	ASSERT(ret == 1, "second uninstall fails");
	ASSERT(file_contains("err.txt", "Package 'tool' is not installed"), "absence reported");

	stderr_begin("err.txt");
	ret = run_cmd(handle_uninstall, "uninstall", nullptr, nullptr);
	stderr_end();
	ASSERT(ret == 1, "missing package argument");
	ASSERT(file_contains("err.txt", "No package specified"), "usage reported");

	sdsfree(lib);
	sdsfree(pkg);
	sdsfree(home);
	sdsfree(repo);
	sandbox_off();
	leave_dir(root);
	PASS();
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

void coffee_register_cov_deps_commands_tests(void)
{
	TEST_REGISTER(cov_dg_constraint_forms);
	TEST_REGISTER(cov_dg_conflicting_constraint_warns);
	TEST_REGISTER(cov_dg_source_forms);
	TEST_REGISTER(cov_dg_version_fallbacks);
	TEST_REGISTER(cov_dg_invalid_root_name);
	TEST_REGISTER(cov_dg_nameless_structured_dep);
	TEST_REGISTER(cov_dg_transitive_self_reference);
	TEST_REGISTER(cov_dg_query_edge_cases);
	TEST_REGISTER(cov_cache_load_all_fields);
	TEST_REGISTER(cov_cache_minimal_node_defaults);
	TEST_REGISTER(cov_cache_rejects_bad_caches);
	TEST_REGISTER(cov_cache_rejects_stale_mtimes);
	TEST_REGISTER(cov_cache_write_all_fields_roundtrip);
	TEST_REGISTER(cov_cache_write_failures);
	TEST_REGISTER(cov_git_compare_remote_states);
	TEST_REGISTER(cov_git_fetch_git_variants);
	TEST_REGISTER(cov_fetch_git_tag_clone_then_update);
	TEST_REGISTER(cov_fetch_git_branch_and_default_ref);
	TEST_REGISTER(cov_fetch_git_failures);
	TEST_REGISTER(cov_fetch_invalid_root_name);
	TEST_REGISTER(cov_fetch_path_dep_variants);
	TEST_REGISTER(cov_fetch_transitive_path_dep);
	TEST_REGISTER(cov_fetch_registry_failures);
	TEST_REGISTER(cov_fetch_offline_git_deps);
	TEST_REGISTER(cov_fetch_head_commit_edge_cases);
	TEST_REGISTER(cov_fetch_link_failure);
	TEST_REGISTER(cov_update_git_dep_follows_origin);
	TEST_REGISTER(cov_update_target_preserves_other_entries);
	TEST_REGISTER(cov_update_failures);
	TEST_REGISTER(cov_outdated_report_rows);
	TEST_REGISTER(cov_outdated_failures);
	TEST_REGISTER(cov_outdated_empty_lockfile);
	TEST_REGISTER(cov_genlock_records_git_commit);
	TEST_REGISTER(cov_genlock_failures);
	TEST_REGISTER(cov_vendor_structured_sources);
	TEST_REGISTER(cov_vendor_flat_entries);
	TEST_REGISTER(cov_install_builds_binaries);
	TEST_REGISTER(cov_install_dep_flag_fallback);
	TEST_REGISTER(cov_install_source_only_and_version_warning);
	TEST_REGISTER(cov_install_rejects_bad_input);
	TEST_REGISTER(cov_install_symlink_failures);
	TEST_REGISTER(cov_install_bin_with_sources_and_layout);
	TEST_REGISTER(cov_install_rejects_unsafe_version);
	TEST_REGISTER(cov_install_rename_fallback_moves_with_mv);
	TEST_REGISTER(cov_install_update_paths);
	TEST_REGISTER(cov_install_update_config_merges_missing_keys);
	TEST_REGISTER(cov_install_update_config_top_level_keys_precede_sections);
	TEST_REGISTER(cov_uninstall_removes_installed_package);
	TEST_REGISTER(cov_add_structured_duplicate_and_flags);
	TEST_REGISTER(cov_add_manifest_failures);
	TEST_REGISTER(cov_remove_failures);
	TEST_REGISTER(cov_uninstall_removal_failure);
}
