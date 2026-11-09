/*
 * Additional unit tests that raise per-function coverage (build_registry).
 *
 * Targets build.c, project.c, registry.c, safeio.h and strings.h.  Every
 * test works inside a mkdtemp() directory and talks to local fixtures only:
 * file:// registries and fake curl/zstd/cc scripts placed first on PATH.
 */

#include "../src/build.h"
#include "../src/lockfile.h"
#include "../src/project.h"
#include "../src/registry.h"
#include "../src/safeio.h"
#include "../src/strings.h"
#include "../src/toolcheck.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef __has_feature
# define __has_feature(x) 0
#endif

void coffee_register_cov_build_registry_tests(void);

static char saved_cwd[4'096];

/* --- helpers --- */

static sds new_tmpdir(void)
{
	sds path = sdsnew("/tmp/cov-build-registry-XXXXXX");
	if (mkdtemp(path) == nullptr) {
		sdsfree(path);
		return nullptr;
	}
	return path;
}

static void make_dirs(const char *path)
{
	sds copy = sdsnew(path);
	for (size_t i = 1; i < sdslen(copy); i++) {
		if (copy[i] == '/') {
			copy[i] = '\0';
			mkdir(copy, 0755);
			copy[i] = '/';
		}
	}
	mkdir(copy, 0755);
	sdsfree(copy);
}

static bool write_file(const char *path, const char *content)
{
	FILE *fp = safe_fopen(path, "w");
	if (fp == nullptr) {
		return false;
	}
	fprintf_safe(fp, "%s", content);
	safe_fclose(fp);
	return true;
}

static bool write_script(const char *path, const char *content)
{
	if (!write_file(path, content)) {
		return false;
	}
	return chmod(path, 0755) == 0;
}

static sds read_file_text(const char *path)
{
	FILE *fp = safe_fopen(path, "r");
	if (fp == nullptr) {
		return nullptr;
	}
	sds    out = sdsempty();
	char   buf[512];
	size_t n = fread(buf, 1, sizeof(buf), fp);
	while (n > 0) {
		out = sdscatlen(out, buf, n);
		n   = fread(buf, 1, sizeof(buf), fp);
	}
	safe_fclose(fp);
	return out;
}

static bool path_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static sds join_path(const char *dir, const char *rest)
{
	return sdscatprintf(sdsempty(), "%s/%s", dir, rest);
}

static sds env_save(const char *key)
{
	const char *value = getenv(key);
	if (value == nullptr) {
		return nullptr;
	}
	return sdsnew(value);
}

static void env_restore(const char *key, sds saved)
{
	if (saved == nullptr) {
		unsetenv(key);
		return;
	}
	setenv(key, saved, 1);
	sdsfree(saved);
}

static bool enter_dir(const char *dir)
{
	if (getcwd(saved_cwd, sizeof(saved_cwd)) == nullptr) {
		return false;
	}
	return chdir(dir) == 0;
}

static void leave_dir(sds root)
{
	if (chdir(saved_cwd) != 0) {
		chdir("/");
	}
	test_remove_tree(root);
	sdsfree(root);
}

/* Put `dir` in front of PATH; returns the previous PATH for env_restore(). */
static sds prepend_path(const char *dir)
{
	sds old     = env_save("PATH");
	sds new_val = sdscatprintf(sdsempty(), "%s:%s", dir, old != nullptr ? old : "/usr/bin:/bin");
	setenv("PATH", new_val, 1);
	sdsfree(new_val);
	return old;
}

/* ===================================================================== */
/* strings.h / safeio.h                                                   */
/* ===================================================================== */

TEST(cov_br_toml_escape_specials)
{
	sds out = toml_escape("a\\b\"c\nd\re\tf\bg\fh\x01 i\x7f");
	ASSERT(strcmp(out, "a\\\\b\\\"c\\nd\\re\\tf\\bg\\fh\\u0001 i\\u007F") == 0, "all specials escaped");
	sdsfree(out);

	out = toml_escape(nullptr);
	ASSERT(sdslen(out) == 0, "nullptr yields empty string");
	sdsfree(out);

	out = toml_escape("plain \xc3\xa9");
	ASSERT(strcmp(out, "plain \xc3\xa9") == 0, "ordinary and high bytes pass through");
	sdsfree(out);
	PASS();
}

TEST(cov_br_toml_escape_manifest_roundtrip)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds path = join_path(root, "Coffee.toml");

	const char *tricky = "q\" b\\ n\n r\r t\t bs\b ff\f c\x01 d\x7f";
	manifest_t  m;
	memset(&m, 0, sizeof(m));
	m.package.name        = sdsnew("esc");
	m.package.version     = sdsnew("1.0.0");
	m.package.description = sdsnew(tricky);
	ASSERT(manifest_write(path, &m) == 0, "manifest written");
	sdsfree(m.package.name);
	sdsfree(m.package.version);
	sdsfree(m.package.description);

	manifest_t *back = manifest_parse(path);
	ASSERT(back != nullptr, "escaped manifest parses");
	ASSERT(back->package.description != nullptr && strcmp(back->package.description, tricky) == 0,
	       "special characters survive the round trip");
	manifest_free(back);

	sdsfree(path);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_safe_fopen_nofollow)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");

	sds   file = join_path(root, "stage.tmp");
	FILE *fp   = safe_fopen_nofollow(file);
	ASSERT(fp != nullptr, "regular staging file opens");
	fprintf_safe(fp, "data");
	safe_fclose(fp);
	sds text = read_file_text(file);
	ASSERT(text != nullptr && strcmp(text, "data") == 0, "content written");
	sdsfree(text);

	sds target = join_path(root, "victim");
	sds link   = join_path(root, "link.tmp");
	ASSERT(write_file(target, "keep"), "victim");
	ASSERT(symlink(target, link) == 0, "symlink");
	ASSERT(safe_fopen_nofollow(link) == nullptr, "symlink is refused");
	text = read_file_text(target);
	ASSERT(text != nullptr && strcmp(text, "keep") == 0, "victim not truncated");
	sdsfree(text);

	sdsfree(file);
	sdsfree(target);
	sdsfree(link);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
# define HOARD_MAX   65'536
# define SPARE_SIZE  4'096
# define SPARE_TAKEN 3'696

static void *hoard[HOARD_MAX];

/* Current address-space size in bytes, or 0 when unknown. */
static rlim_t address_space_size(void)
{
	FILE *fp = safe_fopen("/proc/self/statm", "r");
	if (fp == nullptr) {
		return 0;
	}
	unsigned long pages = 0;
	int           got   = fscanf(fp, "%lu", &pages);
	safe_fclose(fp);
	if (got != 1) {
		return 0;
	}
	return (rlim_t)pages * (rlim_t)sysconf(_SC_PAGESIZE);
}

/* Take every free byte malloc can still hand out, from big blocks down to
 * 16-byte ones; returns the number of blocks held in hoard. */
static size_t exhaust_heap(void)
{
	size_t held = 0;
	for (size_t size = 2'048; size >= 16; size -= 16) {
		void *block = nullptr;
		while (held < HOARD_MAX && posix_memalign(&block, 16, size) == 0) {
			hoard[held++] = block;
		}
	}
	return held;
}

TEST(cov_br_safe_fopen_nofollow_fdopen_failure)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds        path = join_path(root, "Coffee.lock");
	lockfile_t lf   = { 0 };
	lf.version      = 1;

	/* One large spare block, fenced off so it cannot merge into the top of
	 * the heap.  It is far too big for tcache, so freeing it returns it to
	 * malloc's bins. */
	void *spare     = nullptr;
	void *separator = nullptr;
	ASSERT(posix_memalign(&spare, 16, SPARE_SIZE) == 0, "spare block");
	ASSERT(posix_memalign(&separator, 16, 16) == 0, "separator block");

	struct rlimit old_limit;
	ASSERT(getrlimit(RLIMIT_AS, &old_limit) == 0, "getrlimit");
	rlim_t current = address_space_size();
	ASSERT(current > 0, "address space size");
	struct rlimit capped = old_limit;
	capped.rlim_cur      = current;
	ASSERT(setrlimit(RLIMIT_AS, &capped) == 0, "cap the address space");

	/* With the heap exhausted, release the spare block and take most of it
	 * back: the ~400 bytes left over suffice for the strings lockfile_write
	 * builds but are too small for the FILE that fdopen(3) allocates. */
	size_t held = exhaust_heap();
	free(spare);
	void *taken        = nullptr;
	int   taken_status = posix_memalign(&taken, 16, SPARE_TAKEN);
	i64   status       = lockfile_write(path, &lf);

	ASSERT(setrlimit(RLIMIT_AS, &old_limit) == 0, "restore address space limit");
	for (size_t i = 0; i < held; i++) {
		free(hoard[i]);
	}
	free(taken);
	free(separator);
	ASSERT(taken_status == 0, "most of the spare block is reusable");
	ASSERT(held < HOARD_MAX, "heap was exhausted");
	ASSERT(status == -1, "fdopen failure is reported by the writer");
	ASSERT(access(path, F_OK) != 0, "no lockfile written");

	sdsfree(path);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}
#endif

/* ===================================================================== */
/* project.c                                                              */
/* ===================================================================== */

TEST(cov_br_project_find_manifest_cwd_removed)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds gone = join_path(root, "gone");
	ASSERT(mkdir(gone, 0755) == 0, "mkdir");
	ASSERT(enter_dir(gone), "enter");
	ASSERT(rmdir(gone) == 0, "rmdir cwd");

	sds found = project_find_manifest(nullptr);
	ASSERT(found == nullptr, "no manifest when cwd cannot be determined");

	sdsfree(gone);
	leave_dir(root);
	PASS();
}

TEST(cov_br_project_find_manifest_walks_up)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds manifest = join_path(root, "Coffee.toml");
	sds deep     = join_path(root, "a/b");
	ASSERT(write_file(manifest, "[package]\nname = \"x\"\n"), "manifest");
	make_dirs(deep);

	sds found = project_find_manifest(deep);
	ASSERT(found != nullptr && strcmp(found, manifest) == 0, "found in ancestor");
	sdsfree(found);

	sdsfree(manifest);
	sdsfree(deep);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_project_load_manifest_and_name)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds path = join_path(root, "Coffee.toml");
	ASSERT(write_file(path, "[package]\nname = \"loaded\"\nversion = \"1.0.0\"\n"), "manifest");

	manifest_t *m = project_load_manifest(path);
	ASSERT(m != nullptr, "manifest loads");
	sds name = project_get_name(m);
	ASSERT(name != nullptr && strcmp(name, "loaded") == 0, "name returned");
	sdsfree(name);
	manifest_free(m);

	ASSERT(project_get_name(nullptr) == nullptr, "nullptr manifest has no name");
	manifest_t empty;
	memset(&empty, 0, sizeof(empty));
	ASSERT(project_get_name(&empty) == nullptr, "manifest without name has no name");

	sdsfree(path);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_pkg_dir_resolution_order)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");

	sds global = pkg_dir("zlib");
	sds home   = sdscatprintf(sdsempty(), "%s/deps/zlib", coffee_home_dir());
	ASSERT(strcmp(global, home) == 0, "falls back to the global cache");
	sdsfree(global);
	sdsfree(home);

	make_dirs("vendor/zlib");
	sds vendor = pkg_dir("zlib");
	ASSERT(strcmp(vendor, "vendor/zlib") == 0, "vendor/ is next");
	sdsfree(vendor);

	make_dirs("deps/zlib");
	sds local = pkg_dir("zlib");
	ASSERT(strcmp(local, "deps/zlib") == 0, "deps/ wins");
	sdsfree(local);

	leave_dir(root);
	PASS();
}

TEST(cov_br_create_dir_and_file)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");

	sds dir = join_path(root, "made");
	ASSERT(create_dir(dir) == 0, "create new dir");
	ASSERT(path_exists(dir), "dir exists");
	ASSERT(create_dir(dir) == 0, "existing dir is fine");

	sds missing_parent = join_path(root, "no/such/parent");
	ASSERT(create_dir(missing_parent) != 0, "missing parent fails");

	sds file = join_path(dir, "f.txt");
	ASSERT(create_file(file, "hello") == 0, "create file");
	sds text = read_file_text(file);
	ASSERT(text != nullptr && strcmp(text, "hello") == 0, "content written");
	sdsfree(text);

	sds bad = join_path(root, "no/such/f.txt");
	ASSERT(create_file(bad, "x") == -1, "unwritable path fails");

	sdsfree(dir);
	sdsfree(missing_parent);
	sdsfree(file);
	sdsfree(bad);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_resolve_dep_version)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds toml = join_path(root, "library.toml");

	sds v = resolve_dep_version(root);
	ASSERT(strcmp(v, "*") == 0, "missing library.toml means any version");
	sdsfree(v);

	ASSERT(write_file(toml, "version = \"3.2.1\"\n"), "toml");
	v = resolve_dep_version(root);
	ASSERT(strcmp(v, "3.2.1") == 0, "version read");
	sdsfree(v);

	ASSERT(write_file(toml, "name = \"x\"\n"), "toml without version");
	v = resolve_dep_version(root);
	ASSERT(strcmp(v, "*") == 0, "no version key means any version");
	sdsfree(v);

	ASSERT(write_file(toml, "this is = = not toml [[\n"), "broken toml");
	v = resolve_dep_version(root);
	ASSERT(strcmp(v, "*") == 0, "unparseable toml means any version");
	sdsfree(v);

	sdsfree(toml);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

/* ===================================================================== */
/* build.c: process helpers                                               */
/* ===================================================================== */

TEST(cov_br_run_command_verbose_and_signal)
{
	char *ok_argv[] = { "true", nullptr };
	ASSERT(run_command(ok_argv, RUN_CMD_VERBOSE) == 0, "verbose run succeeds");

	char *sig_argv[] = { "sh", "-c", "kill -9 $$", nullptr };
	ASSERT(run_command(sig_argv, RUN_CMD_QUIET) == 1, "death by signal maps to 1");

	char *exit_argv[] = { "sh", "-c", "exit 7", nullptr };
	ASSERT(run_command(exit_argv, 0) == 7, "exit status is propagated");
	PASS();
}

TEST(cov_br_run_command_capture_variants)
{
	char *echo_argv[] = { "sh", "-c", "echo out; echo err >&2", nullptr };
	sds   out         = run_command_capture(echo_argv, RUN_CMD_VERBOSE | RUN_CMD_QUIET);
	ASSERT(out != nullptr && strcmp(out, "out\n") == 0, "stdout captured");
	sdsfree(out);

	char *fail_argv[] = { "sh", "-c", "echo partial; exit 3", nullptr };
	ASSERT(run_command_capture(fail_argv, 0) == nullptr, "non-zero exit yields nullptr");

	char *sig_argv[] = { "sh", "-c", "kill -9 $$", nullptr };
	ASSERT(run_command_capture(sig_argv, 0) == nullptr, "signal death yields nullptr");
	PASS();
}

static void ignore_alarm(int sig)
{
	(void)sig;
}

TEST(cov_br_run_command_capture_survives_eintr)
{
	struct sigaction sa;
	struct sigaction old_sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = ignore_alarm;
	sigemptyset(&sa.sa_mask);
	ASSERT(sigaction(SIGALRM, &sa, &old_sa) == 0, "install handler");

	struct itimerval timer;
	memset(&timer, 0, sizeof(timer));
	timer.it_value.tv_usec = 50'000;
	ASSERT(setitimer(ITIMER_REAL, &timer, nullptr) == 0, "arm timer");

	char *argv[] = { "sh", "-c", "sleep 0.3; echo done", nullptr };
	sds   out    = run_command_capture(argv, 0);

	memset(&timer, 0, sizeof(timer));
	setitimer(ITIMER_REAL, &timer, nullptr);
	sigaction(SIGALRM, &old_sa, nullptr);

	ASSERT(out != nullptr && strcmp(out, "done\n") == 0, "read is retried after EINTR");
	sdsfree(out);
	PASS();
}

static void swap_pipe_for_directory(int sig)
{
	(void)sig;
	int dir_fd = safe_open("/", O_RDONLY | O_DIRECTORY);
	if (dir_fd < 0) {
		return;
	}
	for (int fd = 3; fd < 64; fd++) {
		if (fd != dir_fd && fcntl(fd, F_GETPIPE_SZ) >= 0 && (fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY) {
			dup2(dir_fd, fd);
		}
	}
	safe_close(dir_fd);
}

TEST(cov_br_run_command_capture_read_error)
{
	/* While the parent waits in read(2), a timer signal replaces the pipe's
	 * read end with a directory: the retried read fails with EISDIR. */
	struct sigaction sa;
	struct sigaction old_sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = swap_pipe_for_directory;
	sigemptyset(&sa.sa_mask);
	ASSERT(sigaction(SIGALRM, &sa, &old_sa) == 0, "install handler");

	struct itimerval timer;
	memset(&timer, 0, sizeof(timer));
	timer.it_value.tv_usec = 50'000;
	ASSERT(setitimer(ITIMER_REAL, &timer, nullptr) == 0, "arm timer");

	char *argv[] = { "sh", "-c", "sleep 0.3; echo done", nullptr };
	sds   out    = run_command_capture(argv, RUN_CMD_QUIET);

	memset(&timer, 0, sizeof(timer));
	setitimer(ITIMER_REAL, &timer, nullptr);
	sigaction(SIGALRM, &old_sa, nullptr);

	ASSERT(out == nullptr, "the child dies of SIGPIPE once the read end is gone");
	PASS();
}

TEST(cov_br_run_command_fork_failure)
{
	if (geteuid() == 0) {
		PASS();
	}
	struct rlimit old_limit;
	ASSERT(getrlimit(RLIMIT_NPROC, &old_limit) == 0, "getrlimit");
	struct rlimit none = old_limit;
	none.rlim_cur      = 0;
	ASSERT(setrlimit(RLIMIT_NPROC, &none) == 0, "forbid new processes");

	char *argv[] = { "true", nullptr };
	i64   status = run_command(argv, 0);
	sds   out    = run_command_capture(argv, 0);

	ASSERT(setrlimit(RLIMIT_NPROC, &old_limit) == 0, "restore process limit");
	ASSERT(status == 1, "run_command reports fork failure");
	ASSERT(out == nullptr, "run_command_capture reports fork failure");
	PASS();
}

TEST(cov_br_run_command_capture_pipe_failure)
{
	struct rlimit old_limit;
	ASSERT(getrlimit(RLIMIT_NOFILE, &old_limit) == 0, "getrlimit");
	struct rlimit tiny = old_limit;
	tiny.rlim_cur      = 0;
	ASSERT(setrlimit(RLIMIT_NOFILE, &tiny) == 0, "lower fd limit");

	char *argv[] = { "true", nullptr };
	sds   out    = run_command_capture(argv, 0);

	ASSERT(setrlimit(RLIMIT_NOFILE, &old_limit) == 0, "restore fd limit");
	ASSERT(out == nullptr, "pipe failure yields nullptr");
	PASS();
}

/* ===================================================================== */
/* build.c: compile_sources and flag helpers                              */
/* ===================================================================== */

TEST(cov_br_compile_sources_no_files)
{
	sds output = sdsnew("a.out");
	ASSERT(compile_sources(nullptr, 0, output, "cc", "-O2", false) == 1, "no sources is an error");
	sdsfree(output);
	PASS();
}

TEST(cov_br_compile_sources_builds_argv)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds cc   = join_path(root, "fakecc");
	sds args = join_path(root, "args.txt");
	sds body = sdscatprintf(sdsempty(), "#!/bin/sh\nfor a in \"$@\"; do echo \"$a\"; done > '%s'\n", args);
	ASSERT(write_script(cc, body), "fake compiler");

	sds srcs[2] = { sdsnew("one.c"), sdsnew("two.c") };
	sds output  = sdsnew("prog");
	i64 ret     = compile_sources(srcs, 2, output, cc, "-O2 -DNAME=\"a b\"  -Wall ", true);
	ASSERT(ret == 0, "compiler succeeded");

	sds text = read_file_text(args);
	ASSERT(text != nullptr, "args recorded");
	ASSERT(strcmp(text, "-O2\n-DNAME=a b\n-Wall\n-o\nprog\none.c\ntwo.c\n") == 0, "argv assembled in order");
	sdsfree(text);

	ASSERT(compile_sources(srcs, 1, output, "/nonexistent/cc", nullptr, false) != 0, "missing compiler fails");

	sdsfree(srcs[0]);
	sdsfree(srcs[1]);
	sdsfree(output);
	sdsfree(body);
	sdsfree(args);
	sdsfree(cc);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_flag_tokens_trailing_spaces)
{
	ASSERT(count_flag_tokens("-O2  -g  ") == 2, "trailing blanks are not a token");
	ASSERT(count_flag_tokens("   ") == 0, "only blanks");

	char  *argv[4] = { nullptr, nullptr, nullptr, nullptr };
	size_t end     = 0;
	sds    copy    = split_flags_to_argv("-O2  -l\"x y\"  ", argv, 0, &end);
	ASSERT(end == 2, "two tokens");
	ASSERT(strcmp(argv[0], "-O2") == 0, "first token");
	ASSERT(strcmp(argv[1], "-lx y") == 0, "quoted blank stays inside the token");
	sdsfree(copy);

	copy = split_flags_to_argv("   ", argv, 1, &end);
	ASSERT(end == 1, "blank-only flags add nothing");
	sdsfree(copy);
	PASS();
}

TEST(cov_br_url_scp_style_bad_host)
{
	ASSERT(!url_is_valid("gi!t@host.example:repo.git"), "user with a metacharacter is rejected");
	ASSERT(!url_is_valid("git@ho!st:repo.git"), "host with a metacharacter is rejected");
	ASSERT(url_is_valid("git@host.example:org/repo.git"), "plain scp style is accepted");
	PASS();
}

/* ===================================================================== */
/* build.c: dependency resolution                                         */
/* ===================================================================== */

static bool write_dep_version(const char *home, const char *name, const char *sub, const char *toml)
{
	sds dir = sdscatprintf(sdsempty(), "%s/deps/%s/%s", home, name, sub);
	make_dirs(dir);
	if (toml == nullptr) {
		sdsfree(dir);
		return true;
	}
	sds  file = join_path(dir, "library.toml");
	bool ok   = write_file(file, toml);
	sdsfree(file);
	sdsfree(dir);
	return ok;
}

TEST(cov_br_dep_constraint_picks_highest_satisfying)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds home = join_path(root, "home");
	make_dirs(home);
	sds old_home = env_save("COFFEE_HOME");
	setenv("COFFEE_HOME", home, 1);

	ASSERT(write_dep_version(home, "foo", "1.0.0", "[package]\nversion = \"1.0.0\"\n"), "1.0.0");
	ASSERT(write_dep_version(home, "foo", "1.2.0", "[package]\nversion = \"1.2.0\"\n"), "1.2.0");
	ASSERT(write_dep_version(home, "foo", "1.5.0", "[package]\nversion = \"1.5.0\"\n"), "1.5.0");
	ASSERT(write_dep_version(home, "foo", "2.0.0", "[package]\nversion = \"2.0.0\"\n"), "2.0.0");
	ASSERT(write_dep_version(home, "foo", "1.1.0", "[package]\nversion = \"1.1.0\"\n"), "1.1.0");
	ASSERT(write_dep_version(home, "foo", "nolib", nullptr), "dir without library.toml");
	ASSERT(write_dep_version(home, "foo", "broken", "= = not toml [[\n"), "unparseable toml");
	ASSERT(write_dep_version(home, "foo", "nopkg", "name = \"x\"\n"), "no [package]");
	ASSERT(write_dep_version(home, "foo", "nover", "[package]\nname = \"x\"\n"), "no version");
	ASSERT(write_dep_version(home, "foo", "weird", "[package]\nversion = \"weird\"\n"), "unparseable version");
	ASSERT(write_dep_version(home, "foo", ".hidden", "[package]\nversion = \"9.9.9\"\n"), "hidden");

	sds picked = dep_resolve_dir_constraint("foo", "<2.0.0");
	sds want   = sdscatprintf(sdsempty(), "%s/deps/foo/1.5.0", home);
	ASSERT(picked != nullptr && strcmp(picked, want) == 0, "highest version below 2.0.0 chosen");
	sdsfree(picked);
	sdsfree(want);

	picked = dep_resolve_dir_constraint("foo", ">=1.0.0");
	want   = sdscatprintf(sdsempty(), "%s/deps/foo/2.0.0", home);
	ASSERT(picked != nullptr && strcmp(picked, want) == 0, "highest version overall chosen");
	sdsfree(picked);
	sdsfree(want);

	picked = dep_resolve_dir_constraint("foo", ">=7.0.0");
	want   = sdscatprintf(sdsempty(), "%s/deps/foo", home);
	ASSERT(picked != nullptr && strcmp(picked, want) == 0, "no match falls back to the flat directory");
	sdsfree(picked);
	sdsfree(want);

	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_dep_constraint_local_vendor_and_missing)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds home = join_path(root, "home");
	make_dirs(home);
	sds old_home = env_save("COFFEE_HOME");
	setenv("COFFEE_HOME", home, 1);
	ASSERT(enter_dir(root), "enter");

	ASSERT(dep_resolve_dir_constraint("ghost", ">=1.0") == nullptr, "unknown dependency");

	sds flat = dep_resolve_dir_constraint("ghost", "*");
	ASSERT(flat == nullptr, "wildcard constraint behaves like dep_resolve_dir");

	make_dirs("vendor/bar");
	sds vendor = dep_resolve_dir_constraint("bar", ">=1.0");
	ASSERT(vendor != nullptr && strcmp(vendor, "vendor/bar") == 0, "vendor dir is used");
	sdsfree(vendor);

	make_dirs("deps/bar");
	sds local = dep_resolve_dir_constraint("bar", ">=1.0");
	ASSERT(local != nullptr && strcmp(local, "deps/bar") == 0, "local deps dir wins");
	sdsfree(local);

	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	leave_dir(root);
	PASS();
}

TEST(cov_br_dep_constraint_unreadable_global_dir)
{
	if (geteuid() == 0) {
		PASS();
	}
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds home = join_path(root, "home");
	make_dirs(home);
	sds old_home = env_save("COFFEE_HOME");
	setenv("COFFEE_HOME", home, 1);
	ASSERT(write_dep_version(home, "baz", "1.0.0", "[package]\nversion = \"1.0.0\"\n"), "version dir");
	sds base = join_path(home, "deps/baz");
	ASSERT(chmod(base, 0) == 0, "make unreadable");

	sds picked = dep_resolve_dir_constraint("baz", ">=1.0.0");
	ASSERT(picked != nullptr && strcmp(picked, base) == 0, "unreadable directory still resolves flat");
	sdsfree(picked);

	chmod(base, 0755);
	sdsfree(base);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_dep_add_flags_collects_sources)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds src_dir = join_path(root, "src");
	make_dirs(src_dir);
	sds a = join_path(src_dir, "a.c");
	sds b = join_path(src_dir, "b.c");
	ASSERT(write_file(a, "int a;\n") && write_file(b, "int b;\n"), "sources");

	sds    flags     = sdsempty();
	sds    list[4]   = { nullptr, nullptr, nullptr, nullptr };
	size_t src_count = 0;
	dep_add_flags(root, "mylib", &flags, list, &src_count);
	ASSERT(src_count == 2, "both dependency sources collected");
	ASSERT(strcmp(list[0], a) == 0 && strcmp(list[1], b) == 0, "sources in glob order");
	ASSERT(strstr(flags, "-l\"mylib\"") != nullptr, "-l flag added");
	for (size_t i = 0; i < src_count; i++) {
		sdsfree(list[i]);
	}

	sdsfree(flags);
	sdsfree(a);
	sdsfree(b);
	sdsfree(src_dir);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

/* ===================================================================== */
/* build.c: build_project                                                 */
/* ===================================================================== */

static const char *legacy_manifest = "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n"
                                     "\n[compilation]\nMakefile = \"legacy\"\n";

TEST(cov_br_build_project_legacy_verbose)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");
	ASSERT(write_file("Coffee.toml", legacy_manifest), "manifest");
	ASSERT(write_file("Makefile", "all:\n\t@echo built > built.txt\n"), "Makefile");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");
	opts.verbose       = true;
	opts.jobs          = 2;
	opts.release       = true;
	opts.debug         = true;

	sds exe = nullptr;
	i64 ret = build_project(&opts, "all", &exe);
	ASSERT(ret == 0, "legacy build succeeds");
	ASSERT(path_exists("built.txt"), "make ran the legacy Makefile");
	ASSERT(exe != nullptr && strcmp(exe, "./build/proj") == 0, "executable path resolved against project dir");

	sdsfree(exe);
	sdsfree(opts.manifest_path);
	leave_dir(root);
	PASS();
}

TEST(cov_br_build_project_make_failure)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");
	ASSERT(write_file("Coffee.toml", legacy_manifest), "manifest");
	ASSERT(write_file("Makefile", "all:\n\t@false\n"), "Makefile");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");

	ASSERT(build_project(&opts, "all", nullptr) != 0, "failing make fails the build");

	sdsfree(opts.manifest_path);
	leave_dir(root);
	PASS();
}

TEST(cov_br_build_project_unparseable_manifest)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");
	ASSERT(write_file("Coffee.toml", "[package\nname = = \n"), "manifest");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");

	ASSERT(build_project(&opts, "all", nullptr) == 1, "broken manifest is an error");

	sdsfree(opts.manifest_path);
	leave_dir(root);
	PASS();
}

TEST(cov_br_build_project_makefile_not_writable)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");
	ASSERT(write_file("Coffee.toml", "[package]\nname = \"gen\"\nversion = \"1.0.0\"\nedition = \"c23\"\n"),
	       "manifest");
	make_dirs("src");
	ASSERT(write_file("src/main.c", "int main(void) { return 0; }\n"), "main.c");

	/* Pre-plant the staging path as a symlink: the generator must refuse
	 * to follow it and report failure instead of clobbering the target. */
	sds makefile = sdsnew("Makefile");
	sds staging  = tmp_path_for(makefile);
	ASSERT(symlink("/nonexistent/target", staging) == 0, "plant symlink");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");

	ASSERT(build_project(&opts, "all", nullptr) == 1, "Makefile generation failure fails the build");
	ASSERT(!path_exists("Makefile"), "no Makefile was written");

	sdsfree(staging);
	sdsfree(makefile);
	sdsfree(opts.manifest_path);
	leave_dir(root);
	PASS();
}

/* ===================================================================== */
/* registry.c                                                             */
/* ===================================================================== */

static const char *fixture_index = "[\n"
                                   "{\n"
                                   "  \"name\": \"alpha\",\n"
                                   "  \"version\": \"1.0.0\",\n"
                                   "  \"description\": \"A tiny parser\"\n"
                                   "},\n"
                                   "{\n"
                                   "  \"name\": \"beta\",\n"
                                   "  version: \"2.0.0\",\n"
                                   "  description  : \"Network thing\"\n"
                                   "}\n"
                                   "]\n";

TEST(cov_br_coffee_home_dir_fallbacks)
{
	sds old_coffee = env_save("COFFEE_HOME");
	sds old_home   = env_save("HOME");

	unsetenv("COFFEE_HOME");
	setenv("HOME", "/tmp/cov-br-fake-home", 1);
	ASSERT(strcmp(coffee_home_dir(), "/tmp/cov-br-fake-home/.coffee") == 0, "HOME/.coffee");

	unsetenv("HOME");
	ASSERT(strcmp(coffee_home_dir(), "/tmp/.coffee") == 0, "no HOME falls back to /tmp");

	setenv("COFFEE_HOME", "/tmp/cov-br-explicit", 1);
	ASSERT(strcmp(coffee_home_dir(), "/tmp/cov-br-explicit") == 0, "COFFEE_HOME wins");

	env_restore("COFFEE_HOME", old_coffee);
	env_restore("HOME", old_home);
	PASS();
}

/* Create a private COFFEE_HOME with a fresh packages.json holding `json`. */
static sds setup_index_home(sds root, const char *json, sds *old_home)
{
	sds home = join_path(root, "home");
	make_dirs(home);
	*old_home = env_save("COFFEE_HOME");
	setenv("COFFEE_HOME", home, 1);
	if (json != nullptr) {
		sds index = join_path(home, "packages.json");
		write_file(index, json);
		sdsfree(index);
	}
	return home;
}

TEST(cov_br_registry_search_cached_index)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_home = nullptr;
	sds home     = setup_index_home(root, fixture_index, &old_home);

	recipe_list_t *all = registry_search(nullptr);
	ASSERT(all != nullptr && all->count == 2, "nullptr query lists everything");
	ASSERT(strcmp(all->recipes[0].name, "alpha") == 0, "first name");
	ASSERT(strcmp(all->recipes[1].version, "2.0.0") == 0, "unquoted key parsed");
	ASSERT(strcmp(all->recipes[1].description, "Network thing") == 0, "key with spaces before colon parsed");
	registry_free_recipes(all);

	sds empty_query = sdsempty();
	all             = registry_search(empty_query);
	ASSERT(all != nullptr && all->count == 2, "empty query lists everything");
	registry_free_recipes(all);
	sdsfree(empty_query);

	sds            query = sdsnew("NETWORK");
	recipe_list_t *hit   = registry_search(query);
	ASSERT(hit != nullptr && hit->count == 1, "description match is case-insensitive");
	ASSERT(strcmp(hit->recipes[0].name, "beta") == 0, "matching recipe copied");
	ASSERT(strcmp(hit->recipes[0].version, "2.0.0") == 0, "version copied");
	registry_free_recipes(hit);

	sdsfree(query);
	query = sdsnew("alp");
	hit   = registry_search(query);
	ASSERT(hit != nullptr && hit->count == 1 && strcmp(hit->recipes[0].name, "alpha") == 0, "name match");
	registry_free_recipes(hit);

	sdsfree(query);
	query = sdsnew("zzz-no-such");
	hit   = registry_search(query);
	ASSERT(hit != nullptr && hit->count == 0, "no match yields an empty list");
	registry_free_recipes(hit);
	sdsfree(query);

	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_search_unusable_index)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_home = nullptr;
	sds home     = setup_index_home(root, "", &old_home);

	recipe_list_t *list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "empty index file yields an empty list");
	registry_free_recipes(list);

	sds index = join_path(home, "packages.json");
	remove(index);
	ASSERT(mkdir(index, 0755) == 0, "index is a directory");
	list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "directory as index yields an empty list");
	registry_free_recipes(list);
	rmdir(index);

	if (geteuid() != 0) {
		ASSERT(write_file(index, fixture_index), "index");
		ASSERT(chmod(index, 0) == 0, "unreadable index");
		list = registry_search(nullptr);
		ASSERT(list != nullptr && list->count == 0, "unreadable index yields an empty list");
		registry_free_recipes(list);
		chmod(index, 0644);
	}

	sdsfree(index);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

/* Install fake curl/zstd on PATH.  zstd writes `json` (or a 70 MiB sparse
 * file when json is nullptr); curl succeeds only when `curl_ok`. */
static sds install_fake_index_tools(sds root, bool curl_ok, const char *json)
{
	sds bin = join_path(root, "bin");
	make_dirs(bin);
	sds curl = join_path(bin, "curl");
	sds zstd = join_path(bin, "zstd");
	if (curl_ok) {
		write_script(curl, "#!/bin/sh\nfor a in \"$@\"; do last=$a; done\nprintf zst > \"$last\"\n");
	} else {
		write_script(curl, "#!/bin/sh\nexit 22\n");
	}
	sds body;
	if (json != nullptr) {
		body = sdscatprintf(sdsempty(),
		                    "#!/bin/sh\nfor a in \"$@\"; do last=$a; done\ncat > \"$last\" <<'EOF'\n%sEOF\n", json);
	} else {
		body = sdsnew("#!/bin/sh\nfor a in \"$@\"; do last=$a; done\ntruncate -s 70M \"$last\"\n");
	}
	write_script(zstd, body);
	sds old_path = prepend_path(bin);
	sdsfree(body);
	sdsfree(zstd);
	sdsfree(curl);
	sdsfree(bin);
	return old_path;
}

TEST(cov_br_registry_search_download_failure)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_home = nullptr;
	sds home     = setup_index_home(root, nullptr, &old_home);
	sds old_path = install_fake_index_tools(root, false, fixture_index);

	recipe_list_t *list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "failed download yields an empty list");
	registry_free_recipes(list);

	env_restore("PATH", old_path);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_search_refreshes_stale_index)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds            old_home = nullptr;
	sds            home     = setup_index_home(root, "[]\n", &old_home);
	sds            index    = join_path(home, "packages.json");
	struct timeval old_times[2];
	memset(old_times, 0, sizeof(old_times));
	old_times[0].tv_sec = time(nullptr) - 3'600;
	old_times[1].tv_sec = time(nullptr) - 3'600;
	ASSERT(utimes(index, old_times) == 0, "age the index");
	sds old_path = install_fake_index_tools(root, true, fixture_index);

	recipe_list_t *list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 2, "stale index was re-downloaded and parsed");
	registry_free_recipes(list);

	env_restore("PATH", old_path);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(index);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_search_short_read)
{
	/* A sysfs attribute reports a 4096-byte size but yields a few bytes. */
	const char *attribute = "/sys/kernel/uevent_seqnum";
	if (access(attribute, R_OK) != 0) {
		PASS();
	}
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds            old_home = nullptr;
	sds            home     = setup_index_home(root, "[]\n", &old_home);
	sds            index    = join_path(home, "packages.json");
	struct timeval old_times[2];
	memset(old_times, 0, sizeof(old_times));
	old_times[0].tv_sec = time(nullptr) - 3'600;
	old_times[1].tv_sec = time(nullptr) - 3'600;
	ASSERT(utimes(index, old_times) == 0, "age the index");
	sds old_path = install_fake_index_tools(root, true, "[]\n");

	/* The fake decompressor replaces the index with a link to the attribute. */
	sds zstd = join_path(root, "bin/zstd");
	sds body = sdscatprintf(
	    sdsempty(), "#!/bin/sh\nfor a in \"$@\"; do last=$a; done\nrm -f \"$last\"\nln -s %s \"$last\"\n", attribute);
	ASSERT(write_script(zstd, body), "fake zstd");

	recipe_list_t *list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "short read yields an empty list");
	registry_free_recipes(list);

	env_restore("PATH", old_path);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(body);
	sdsfree(zstd);
	sdsfree(index);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_search_rejects_decompression_bomb)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_home = nullptr;
	sds home     = setup_index_home(root, nullptr, &old_home);
	sds old_path = install_fake_index_tools(root, true, nullptr);

	recipe_list_t *list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "oversized index is rejected");
	registry_free_recipes(list);
	sds index = join_path(home, "packages.json");
	ASSERT(!path_exists(index), "oversized index was removed");

	sdsfree(index);
	env_restore("PATH", old_path);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

/* Create <root>/reg/recipes/<first>/<name>/<file> and point the registry at it. */
static sds setup_registry(sds root, const char *name, const char *file, const char *content, sds *old_url)
{
	sds reg = join_path(root, "reg");
	sds dir = sdscatprintf(sdsempty(), "%s/recipes/%c/%s", reg, name[0], name);
	make_dirs(dir);
	if (file != nullptr) {
		sds path = join_path(dir, file);
		write_file(path, content);
		sdsfree(path);
	}
	sdsfree(dir);
	*old_url = env_save("COFFEE_REGISTRY_URL");
	sds url  = sdscatprintf(sdsempty(), "file://%s", reg);
	setenv("COFFEE_REGISTRY_URL", url, 1);
	sdsfree(url);
	return reg;
}

static const char *full_recipe = "version = \"1.4.2\"\n"
                                 "license = \"MIT\"\n"
                                 "description = \"A fine library\"\n"
                                 "recipe_url = \"https://example.com/fine.git\"\n"
                                 "dependencies = \"zlib\"\n";

TEST(cov_br_registry_get_all_fields)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_url = nullptr;
	sds reg     = setup_registry(root, "fine", "library.toml", full_recipe, &old_url);

	sds       name = sdsnew("fine");
	recipe_t *r    = registry_get(name);
	ASSERT(r != nullptr, "recipe found");
	ASSERT(strcmp(r->name, "fine") == 0, "name");
	ASSERT(strcmp(r->version, "1.4.2") == 0, "version");
	ASSERT(strcmp(r->license, "MIT") == 0, "license");
	ASSERT(strcmp(r->description, "A fine library") == 0, "description");
	ASSERT(strcmp(r->download_url, "https://example.com/fine.git") == 0, "recipe_url");
	ASSERT(strcmp(r->dependencies, "zlib") == 0, "dependencies");
	registry_free_recipe(r);

	sds missing = sdsnew("absent");
	ASSERT(registry_get(missing) == nullptr, "unknown recipe");
	sds empty = sdsempty();
	ASSERT(registry_get(empty) == nullptr, "empty name");
	ASSERT(registry_get(nullptr) == nullptr, "nullptr name");

	sdsfree(missing);
	sdsfree(empty);
	sdsfree(name);
	env_restore("COFFEE_REGISTRY_URL", old_url);
	sdsfree(reg);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_get_versions_variants)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_url = nullptr;
	sds reg     = setup_registry(root, "fine", "library.toml", full_recipe, &old_url);

	sds             name = sdsnew("fine");
	version_list_t *v    = registry_get_versions(name);
	ASSERT(v != nullptr && v->count == 1, "one version");
	ASSERT(strcmp(v->versions[0], "1.4.2") == 0, "version value");
	registry_free_versions(v);

	sds noversion = sdscatprintf(sdsempty(), "%s/recipes/n/nover/library.toml", reg);
	make_dirs(reg);
	sds nover_dir = sdscatprintf(sdsempty(), "%s/recipes/n/nover", reg);
	make_dirs(nover_dir);
	ASSERT(write_file(noversion, "license = \"MIT\"\n"), "recipe without version");
	sds nover_name = sdsnew("nover");
	v              = registry_get_versions(nover_name);
	ASSERT(v != nullptr && v->count == 0 && v->versions == nullptr, "no version means an empty list");
	registry_free_versions(v);

	sds broken_dir = sdscatprintf(sdsempty(), "%s/recipes/b/broken", reg);
	make_dirs(broken_dir);
	sds broken_file = join_path(broken_dir, "library.toml");
	ASSERT(write_file(broken_file, "= = [[ not toml\n"), "broken recipe");
	sds broken_name = sdsnew("broken");
	v               = registry_get_versions(broken_name);
	ASSERT(v != nullptr && v->count == 0, "unparseable recipe means an empty list");
	registry_free_versions(v);

	sds missing = sdsnew("absent");
	ASSERT(registry_get_versions(missing) == nullptr, "unknown recipe");
	sds empty = sdsempty();
	ASSERT(registry_get_versions(empty) == nullptr, "empty name");
	ASSERT(registry_get_versions(nullptr) == nullptr, "nullptr name");

	sdsfree(missing);
	sdsfree(empty);
	sdsfree(broken_name);
	sdsfree(broken_file);
	sdsfree(broken_dir);
	sdsfree(nover_name);
	sdsfree(nover_dir);
	sdsfree(noversion);
	sdsfree(name);
	env_restore("COFFEE_REGISTRY_URL", old_url);
	sdsfree(reg);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_fetch_downloads_recipe)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_url = nullptr;
	sds reg     = setup_registry(root, "fine", "library.toml", full_recipe, &old_url);
	sds script  = sdscatprintf(sdsempty(), "%s/recipes/f/fine/install.sh", reg);
	ASSERT(write_file(script, "echo install\n"), "install.sh");

	sds name = sdsnew("fine");
	sds dest = join_path(root, "out/fine");
	ASSERT(registry_fetch(name, "1.4.2", dest) == 0, "fetch succeeds");

	sds toml_path = join_path(dest, "library.toml");
	sds text      = read_file_text(toml_path);
	ASSERT(text != nullptr && strstr(text, "1.4.2") != nullptr, "library.toml downloaded");
	sdsfree(text);
	sds install_path = join_path(dest, "install.sh");
	text             = read_file_text(install_path);
	ASSERT(text != nullptr && strcmp(text, "echo install\n") == 0, "install.sh downloaded");
	sdsfree(text);

	/* A recipe without install.sh is still fetched successfully. */
	sds bare_dir = sdscatprintf(sdsempty(), "%s/recipes/b/bare", reg);
	make_dirs(bare_dir);
	sds bare_toml = join_path(bare_dir, "library.toml");
	ASSERT(write_file(bare_toml, "version = \"0.1.0\"\n"), "bare recipe");
	sds bare_name = sdsnew("bare");
	sds bare_dest = join_path(root, "out/bare");
	ASSERT(registry_fetch(bare_name, nullptr, bare_dest) == 0, "fetch without install.sh succeeds");
	sds bare_install = join_path(bare_dest, "install.sh");
	ASSERT(!path_exists(bare_install), "no install.sh was created");

	sdsfree(bare_install);
	sdsfree(bare_dest);
	sdsfree(bare_name);
	sdsfree(bare_toml);
	sdsfree(bare_dir);
	sdsfree(install_path);
	sdsfree(toml_path);
	sdsfree(dest);
	sdsfree(name);
	sdsfree(script);
	env_restore("COFFEE_REGISTRY_URL", old_url);
	sdsfree(reg);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_fetch_failures)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_url = nullptr;
	sds reg     = setup_registry(root, "fine", "library.toml", full_recipe, &old_url);

	sds name = sdsnew("fine");
	sds dest = join_path(root, "out");
	ASSERT(registry_fetch(nullptr, nullptr, dest) == -1, "nullptr name");
	ASSERT(registry_fetch(name, nullptr, nullptr) == -1, "nullptr destination");

	sds missing = sdsnew("absent");
	ASSERT(registry_fetch(missing, nullptr, dest) == -1, "unknown recipe fails");

	sds blocker = join_path(root, "blocker");
	ASSERT(write_file(blocker, "file"), "regular file");
	sds under_file = join_path(blocker, "dest");
	ASSERT(registry_fetch(name, nullptr, under_file) == -1, "destination under a file fails");

	sdsfree(under_file);
	sdsfree(blocker);
	sdsfree(missing);
	sdsfree(dest);
	sdsfree(name);
	env_restore("COFFEE_REGISTRY_URL", old_url);
	sdsfree(reg);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_source_url_cases)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_url = nullptr;
	sds reg     = setup_registry(root, "fine", "library.toml", full_recipe, &old_url);

	bool found = true;
	ASSERT(registry_source_url("../evil", &found) == nullptr && !found, "invalid name rejected");
	ASSERT(registry_source_url(nullptr, nullptr) == nullptr, "nullptr name rejected");
	ASSERT(registry_source_url("absent", &found) == nullptr && !found, "unknown recipe not found");

	sds url = registry_source_url("fine", &found);
	ASSERT(url != nullptr && found, "valid https recipe url returned");
	ASSERT(strcmp(url, "https://example.com/fine.git") == 0, "url value");
	sdsfree(url);

	sds http_dir = sdscatprintf(sdsempty(), "%s/recipes/h/http", reg);
	make_dirs(http_dir);
	sds http_file = join_path(http_dir, "library.toml");
	ASSERT(write_file(http_file, "recipe_url = \"http://example.com/x.git\"\n"), "http recipe");
	ASSERT(registry_source_url("http", &found) == nullptr, "http url refused");
	ASSERT(found, "but the recipe was found");

	sds local_dir = sdscatprintf(sdsempty(), "%s/recipes/l/local", reg);
	make_dirs(local_dir);
	sds local_file = join_path(local_dir, "library.toml");
	ASSERT(write_file(local_file, "recipe_url = \"/srv/git/local.git\"\n"), "local recipe");
	url = registry_source_url("local", &found);
	ASSERT(url != nullptr && strcmp(url, "/srv/git/local.git") == 0, "local path accepted for a local registry");
	sdsfree(url);

	setenv("COFFEE_REGISTRY_URL", "https://example.invalid/registry", 1);
	ASSERT(!registry_recipe_url_is_valid("/srv/git/local.git"), "local path refused for a remote registry");
	ASSERT(registry_recipe_url_is_valid("https://example.com/x.git"), "remote url accepted");

	sdsfree(local_file);
	sdsfree(local_dir);
	sdsfree(http_file);
	sdsfree(http_dir);
	env_restore("COFFEE_REGISTRY_URL", old_url);
	sdsfree(reg);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

/* ===================================================================== */
/* build.c: validators                                                    */
/* ===================================================================== */

#define ALNUM "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

/* Feed every non-zero byte, wrapped in `prefix` and `suffix`, to `check`
 * and compare each verdict with membership in `allowed`.  Returns the
 * first byte that disagrees, or 0 when all of them agree. */
static int first_mismatch(bool (*check)(const char *), const char *prefix, const char *suffix, const char *allowed)
{
	for (int c = 1; c < 256; c++) {
		char one[2] = { (char)c, '\0' };
		sds  input  = sdscatprintf(sdsempty(), "%s%s%s", prefix, one, suffix);
		bool want   = strchr(allowed, c) != nullptr;
		bool got    = check(input);
		sdsfree(input);
		if (got != want) {
			return c;
		}
	}
	return 0;
}

TEST(cov_br_validators_every_byte)
{
	ASSERT(first_mismatch(dep_name_is_valid, "a", "", ALNUM "_-") == 0, "dep name charset");
	ASSERT(first_mismatch(build_target_is_valid, "a", "", ALNUM "_.-") == 0, "build target charset");
	ASSERT(first_mismatch(version_is_valid, "1", "", ALNUM "._+~*-") == 0, "version charset");
	ASSERT(first_mismatch(ref_is_valid, "a", "", ALNUM "._/-") == 0, "git ref charset");
	ASSERT(first_mismatch(ref_is_rev, "abcdef0", "", "0123456789abcdefABCDEF") == 0, "revision charset");
	ASSERT(first_mismatch(url_is_valid, "u", "@h:p", ALNUM "._-") == 0, "scp user charset");
	ASSERT(first_mismatch(url_is_valid, "u@h", ":p", ALNUM "._-") == 0, "scp host charset");
	ASSERT(first_mismatch(url_is_valid, "u@h:p", "", ALNUM "./_-~+") == 0, "scp path charset");
	PASS();
}

TEST(cov_br_validators_edges)
{
	ASSERT(!dep_name_is_valid(nullptr) && !dep_name_is_valid(""), "empty dep name");
	ASSERT(!build_target_is_valid(nullptr) && !build_target_is_valid(""), "empty target");
	ASSERT(!build_target_is_valid("-all"), "target looking like an option");
	ASSERT(!version_is_valid(nullptr) && !version_is_valid(""), "empty version");
	ASSERT(!version_is_valid(".1") && !version_is_valid("-1"), "leading dot or dash");
	ASSERT(!ref_is_valid(nullptr) && !ref_is_valid(""), "empty ref");
	ASSERT(!ref_is_valid("-main"), "ref looking like an option");
	ASSERT(!ref_is_rev(nullptr) && !ref_is_rev("abcdef"), "nullptr or short ref is no revision");
	ASSERT(ref_lowercase(nullptr) == nullptr, "nullptr ref lowercases to nullptr");
	sds lower = ref_lowercase("ABCdef0");
	ASSERT(strcmp(lower, "abcdef0") == 0, "ref lowercased");
	sdsfree(lower);

	sds name = dep_parse_name("zlib   = \"1.0\"");
	ASSERT(strcmp(name, "zlib") == 0, "blanks before '=' are trimmed");
	sdsfree(name);
	name = dep_parse_name("= \"1.0\"");
	ASSERT(sdslen(name) == 0, "nothing before '=' gives an empty name");
	sdsfree(name);
	name = dep_parse_name("plain");
	ASSERT(strcmp(name, "plain") == 0, "bare name");
	sdsfree(name);
	PASS();
}

TEST(cov_br_url_is_valid_forms)
{
	ASSERT(!url_is_valid(nullptr), "nullptr url");
	ASSERT(url_is_valid("git://host/x") && url_is_valid("ssh://host/x"), "git and ssh schemes");
	ASSERT(url_is_valid("git+ssh://host/x") && url_is_valid("git+https://host/x"), "git+ schemes");
	ASSERT(url_is_valid("/srv/x") && url_is_valid("./x") && url_is_valid("../x"), "local paths");
	ASSERT(!url_is_valid("ftp://host/x"), "unknown scheme");
	ASSERT(!url_is_valid("hostpath"), "no '@'");
	ASSERT(!url_is_valid("@host:path"), "empty user");
	ASSERT(!url_is_valid("user@"), "nothing after '@'");
	ASSERT(!url_is_valid("user@host"), "no ':'");
	ASSERT(!url_is_valid("user@host:"), "empty path");
	ASSERT(!url_is_valid("user@host:-path"), "path looking like an option");

	ASSERT(!url_is_valid_remote(nullptr), "nullptr remote");
	ASSERT(!url_is_valid_remote("ftp://host/x"), "invalid remote");
	ASSERT(!url_is_valid_remote("/srv/x") && !url_is_valid_remote("./x") && !url_is_valid_remote("../x"),
	       "local paths are not remote");
	ASSERT(url_is_valid_remote("https://host/x") && url_is_valid_remote("git@host:x"), "remote urls");
	PASS();
}

/* ===================================================================== */
/* build.c: more process, resolution and flag paths                      */
/* ===================================================================== */

TEST(cov_br_run_command_exec_failure_redirects)
{
	char *argv[] = { "/nonexistent/coffee-no-such-tool", nullptr };
	ASSERT(run_command(argv, RUN_CMD_QUIET) == 1, "quiet exec failure");
	ASSERT(run_command(argv, RUN_CMD_NOOUT) == 1, "no-output exec failure");
	ASSERT(run_command(argv, RUN_CMD_QUIET | RUN_CMD_NOOUT) == 1, "silenced exec failure");
	PASS();
}

TEST(cov_br_dep_constraint_empty_means_flat)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds home = join_path(root, "home");
	make_dirs(home);
	sds old_home = env_save("COFFEE_HOME");
	setenv("COFFEE_HOME", home, 1);
	ASSERT(enter_dir(root), "enter");

	ASSERT(dep_resolve_dir_constraint("ghost", nullptr) == nullptr, "nullptr constraint, unknown dep");
	ASSERT(dep_resolve_dir_constraint("ghost", "") == nullptr, "empty constraint, unknown dep");

	make_dirs("deps/ghost");
	sds dir = dep_resolve_dir_constraint("ghost", nullptr);
	ASSERT(dir != nullptr && strcmp(dir, "deps/ghost") == 0, "nullptr constraint resolves flat");
	sdsfree(dir);
	dir = dep_resolve_dir_constraint("ghost", "");
	ASSERT(dir != nullptr && strcmp(dir, "deps/ghost") == 0, "empty constraint resolves flat");
	sdsfree(dir);

	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	leave_dir(root);
	PASS();
}

TEST(cov_br_dep_add_flags_non_string_entries)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds one = join_path(root, "one");
	sds two = join_path(root, "two");
	make_dirs(one);
	make_dirs(two);
	sds one_toml = join_path(one, "library.toml");
	sds two_toml = join_path(two, "library.toml");
	ASSERT(write_file(one_toml, "include = [[\"nested\"]]\nlib = [1, 2]\n"), "toml one");
	ASSERT(write_file(two_toml, "include = [3]\nlib = [[\"nested\"]]\n"), "toml two");

	sds    flags = sdsempty();
	size_t count = 0;
	ASSERT(dep_add_flags(one, "one", &flags, nullptr, &count) == 0, "no usable include or lib entry");
	ASSERT(strcmp(flags, " -l\"one\"") == 0, "only the -l flag");
	ASSERT(count == 0, "no source list, nothing collected");

	sdsfree(flags);
	flags       = sdsempty();
	sds list[1] = { nullptr };
	ASSERT(dep_add_flags(two, "two", &flags, list, nullptr) == 0, "no usable entry either");
	ASSERT(dep_add_flags(two, "two", &flags, list, &count) == 0, "sources searched");
	ASSERT(count == 0 && list[0] == nullptr, "no src/ directory, no sources");
	ASSERT(strcmp(flags, " -l\"two\" -l\"two\"") == 0, "only -l flags");

	sdsfree(flags);
	sdsfree(one_toml);
	sdsfree(two_toml);
	sdsfree(one);
	sdsfree(two);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

/* ===================================================================== */
/* build.c: more build_project paths                                      */
/* ===================================================================== */

static const char *flags_makefile = "all:\n\t@echo '$(CFLAGS_EXTRA)' > flags.txt\n";

TEST(cov_br_build_project_legacy_feature_flags)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");
	sds saved_makeflags = env_save("MAKEFLAGS");
	unsetenv("MAKEFLAGS");
	ASSERT(write_file("Coffee.toml", "[package]\nname = \"feat\"\nversion = \"1.0.0\"\nedition = \"c23\"\n"
	                                 "\n[features]\nfast-mode = []\nlog = []\n"
	                                 "\n[compilation]\nMakefile = \"legacy\"\n"),
	       "manifest");
	ASSERT(write_file("Makefile", flags_makefile), "Makefile");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");
	opts.features      = sdsnew("fast-mode,log");

	ASSERT(build_project(&opts, "all", nullptr) == 0, "legacy build with features succeeds");
	sds text = read_file_text("flags.txt");
	ASSERT(text != nullptr, "flags recorded");
	ASSERT(strcmp(text, "-DFEATURE_FAST_MODE -DFEATURE_LOG\n") == 0 ||
	           strcmp(text, "-DFEATURE_LOG -DFEATURE_FAST_MODE\n") == 0,
	       "both feature defines, blank-separated");
	sdsfree(text);

	sdsfree(opts.features);
	sdsfree(opts.manifest_path);
	env_restore("MAKEFLAGS", saved_makeflags);
	leave_dir(root);
	PASS();
}

TEST(cov_br_build_project_legacy_no_features_default_target)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");
	sds saved_makeflags = env_save("MAKEFLAGS");
	unsetenv("MAKEFLAGS");
	ASSERT(write_file("Coffee.toml", legacy_manifest), "manifest");
	ASSERT(write_file("Makefile", flags_makefile), "Makefile");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");
	opts.all_features  = true;

	ASSERT(build_project(&opts, "", nullptr) == 0, "empty target runs the default goal");
	sds text = read_file_text("flags.txt");
	ASSERT(text != nullptr && strcmp(text, "\n") == 0, "no feature defines passed");
	sdsfree(text);

	sdsfree(opts.manifest_path);
	env_restore("MAKEFLAGS", saved_makeflags);
	leave_dir(root);
	PASS();
}

TEST(cov_br_build_project_non_legacy_makefile_value)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	ASSERT(enter_dir(root), "enter");
	sds saved_makeflags = env_save("MAKEFLAGS");
	unsetenv("MAKEFLAGS");
	ASSERT(write_file("Coffee.toml", "[package]\nname = \"gen\"\nversion = \"1.0.0\"\nedition = \"c23\"\n"
	                                 "\n[compilation]\nMakefile = \"custom\"\n"),
	       "manifest");
	make_dirs("src");
	ASSERT(write_file("src/main.c", "int main(void) { return 0; }\n"), "main.c");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");

	ASSERT(build_project(&opts, "clean", nullptr) == 0, "clean succeeds");
	ASSERT(path_exists("Makefile"), "a value other than legacy generates the Makefile");

	sdsfree(opts.manifest_path);
	env_restore("MAKEFLAGS", saved_makeflags);
	leave_dir(root);
	PASS();
}

TEST(cov_br_build_project_offline_missing_locked_dep)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds home = join_path(root, "home");
	make_dirs(home);
	sds old_home = env_save("COFFEE_HOME");
	setenv("COFFEE_HOME", home, 1);
	ASSERT(enter_dir(root), "enter");
	ASSERT(write_file("Coffee.toml", "[package]\nname = \"proj\"\nversion = \"1.0.0\"\nedition = \"c23\"\n"
	                                 "\n[dependencies]\nfoo = \"*\"\nbar = \"*\"\n"
	                                 "\n[compilation]\nMakefile = \"legacy\"\n"),
	       "manifest");
	ASSERT(write_file("Makefile", "all:\n\t@echo built > built.txt\n"), "Makefile");

	lockfile_dep_t deps[2];
	memset(deps, 0, sizeof(deps));
	deps[0].name    = sdsnew("foo");
	deps[0].version = sdsnew("1.0.0");
	deps[0].path    = join_path(root, "gone/foo");
	deps[1].name    = sdsnew("bar");
	deps[1].version = sdsnew("1.0.0");
	deps[1].path    = join_path(root, "gone/bar");
	lockfile_t lf   = { 0 };
	lf.version      = 1;
	lf.deps         = deps;
	lf.deps_count   = 2;
	sds lock_path   = sdsnew("Coffee.lock");
	ASSERT(lockfile_write(lock_path, &lf) == 0, "lockfile written");

	options opts;
	memset(&opts, 0, sizeof(opts));
	opts.manifest_path = sdsnew("Coffee.toml");
	opts.offline       = true;

	ASSERT(build_project(&opts, "all", nullptr) == 1, "a missing locked dependency fails offline");
	ASSERT(!path_exists("built.txt"), "make did not run");

	for (size_t i = 0; i < 2; i++) {
		sdsfree(deps[i].name);
		sdsfree(deps[i].version);
		sdsfree(deps[i].path);
	}
	sdsfree(lock_path);
	sdsfree(opts.manifest_path);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	leave_dir(root);
	PASS();
}

/* ===================================================================== */
/* registry.c, toolcheck.c, strings.h: more paths                         */
/* ===================================================================== */

static const char *odd_index = "[\n"
                               "{\n"
                               "\t\"name\": \"gamma\",\n"
                               "  \"versionx\": \"9.9.9\",\n"
                               "  \"other\": \"x\",\n"
                               "\n"
                               "  junk: 1,\n"
                               "  \"version\"\t : \"3.1.0\",\n"
                               "  \"description\" \"no separator\",\n"
                               "  \"description\":\t\"\",\n"
                               "  \"description\": 42,\n"
                               "  \"description\": \"unterminated\n"
                               "  \"description\" = \"Gamma tool\"\n"
                               "},\n"
                               "{\t\"name\": \"first-line\",\n"
                               "  \"version\": \"0.1.0\"\n"
                               "},\n"
                               "{\t\"name\": \"hidden\",\n"
                               "  \"description\": \"Gadget maker\"\n"
                               "},\n"
                               "{\n"
                               "  \"name\": \"epsilon\",\n"
                               "  \"description\": \"cut off}\n"
                               "]\n";

TEST(cov_br_registry_index_odd_entries)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_home = nullptr;
	sds home     = setup_index_home(root, odd_index, &old_home);

	recipe_list_t *all = registry_search(nullptr);
	ASSERT(all != nullptr && all->count == 4, "four objects found");
	recipe_t *r = &all->recipes[0];
	ASSERT(r->name != nullptr && strcmp(r->name, "gamma") == 0, "tab-indented name");
	ASSERT(r->version != nullptr && strcmp(r->version, "3.1.0") == 0, "near-miss keys skipped");
	ASSERT(r->description != nullptr && strcmp(r->description, "Gamma tool") == 0, "malformed values skipped");
	r = &all->recipes[1];
	ASSERT(r->name == nullptr, "a key on the brace line is not seen");
	ASSERT(r->version != nullptr && strcmp(r->version, "0.1.0") == 0, "version of the second object");
	ASSERT(r->description == nullptr, "no description");
	r = &all->recipes[3];
	ASSERT(r->name != nullptr && strcmp(r->name, "epsilon") == 0, "name of the cut object");
	ASSERT(r->version == nullptr && r->description == nullptr, "string cut by the closing brace");
	registry_free_recipes(all);

	sds            query = sdsnew("gadget");
	recipe_list_t *hit   = registry_search(query);
	ASSERT(hit != nullptr && hit->count == 1, "description match without a name");
	ASSERT(hit->recipes[0].name == nullptr && hit->recipes[0].version == nullptr, "missing fields stay nullptr");
	ASSERT(strcmp(hit->recipes[0].description, "Gadget maker") == 0, "description copied");
	registry_free_recipes(hit);
	sdsfree(query);

	query = sdsnew("EPSIL");
	hit   = registry_search(query);
	ASSERT(hit != nullptr && hit->count == 1, "name match without description");
	ASSERT(hit->recipes[0].description == nullptr, "no description copied");
	registry_free_recipes(hit);
	sdsfree(query);

	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_search_oversized_cached_index)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_home = nullptr;
	sds home     = setup_index_home(root, "[]\n", &old_home);
	sds index    = join_path(home, "packages.json");
	ASSERT(truncate(index, 70L * 1'024 * 1'024) == 0, "sparse 70 MiB index");

	recipe_list_t *list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "oversized fresh index is ignored");
	registry_free_recipes(list);

	sdsfree(index);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_search_decompressor_failures)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_home = nullptr;
	sds home     = setup_index_home(root, nullptr, &old_home);
	sds old_path = install_fake_index_tools(root, true, fixture_index);
	sds zstd     = join_path(root, "bin/zstd");
	sds index    = join_path(home, "packages.json");

	ASSERT(write_script(zstd, "#!/bin/sh\nexit 1\n"), "failing zstd");
	recipe_list_t *list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "decompression failure yields an empty list");
	registry_free_recipes(list);

	ASSERT(write_script(zstd, "#!/bin/sh\nexit 0\n"), "zstd that writes nothing");
	list = registry_search(nullptr);
	ASSERT(list != nullptr && list->count == 0, "missing decompressed index yields an empty list");
	registry_free_recipes(list);
	ASSERT(!path_exists(index), "no index left behind");

	sdsfree(index);
	sdsfree(zstd);
	env_restore("PATH", old_path);
	env_restore("COFFEE_HOME", old_home);
	sdsfree(home);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_default_url_and_broken_recipe)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_url = nullptr;
	sds reg     = setup_registry(root, "broken", "library.toml", "= = [[ not toml\n", &old_url);

	sds       name = sdsnew("broken");
	recipe_t *r    = registry_get(name);
	ASSERT(r != nullptr && strcmp(r->name, "broken") == 0, "recipe found");
	ASSERT(r->version == nullptr && r->description == nullptr, "unparseable recipe has no fields");
	registry_free_recipe(r);

	/* An empty override selects the built-in registry; a failing fake
	 * curl keeps the request off the network. */
	sds old_path = install_fake_index_tools(root, false, "[]\n");
	setenv("COFFEE_REGISTRY_URL", "", 1);
	ASSERT(registry_get(name) == nullptr, "default registry unreachable");
	ASSERT(registry_get_versions(name) == nullptr, "no versions either");
	env_restore("PATH", old_path);

	sdsfree(name);
	env_restore("COFFEE_REGISTRY_URL", old_url);
	sdsfree(reg);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_local_path_registries)
{
	sds old_url = env_save("COFFEE_REGISTRY_URL");

	setenv("COFFEE_REGISTRY_URL", "/srv/registry", 1);
	ASSERT(registry_recipe_url_is_valid("/srv/git/x.git"), "absolute registry path");
	setenv("COFFEE_REGISTRY_URL", "./registry", 1);
	ASSERT(registry_recipe_url_is_valid("./x.git"), "relative registry path");
	setenv("COFFEE_REGISTRY_URL", "../registry", 1);
	ASSERT(registry_recipe_url_is_valid("../x.git"), "parent registry path");
	setenv("COFFEE_REGISTRY_URL", "registry", 1);
	ASSERT(!registry_recipe_url_is_valid("/srv/git/x.git"), "bare word is no local registry");
	ASSERT(!registry_recipe_url_is_valid("http://example.com/x.git"), "invalid url");

	env_restore("COFFEE_REGISTRY_URL", old_url);
	PASS();
}

TEST(cov_br_registry_source_url_without_flag)
{
	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds old_url = nullptr;
	sds reg     = setup_registry(root, "fine", "library.toml", full_recipe, &old_url);

	bool found = true;
	ASSERT(registry_source_url("", &found) == nullptr && !found, "empty name rejected");
	sds url = registry_source_url("fine", nullptr);
	ASSERT(url != nullptr && strcmp(url, "https://example.com/fine.git") == 0, "url without found flag");
	sdsfree(url);

	env_restore("COFFEE_REGISTRY_URL", old_url);
	sdsfree(reg);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

TEST(cov_br_registry_free_sparse_records)
{
	registry_free_recipe(nullptr);
	recipe_t *r = safe_calloc(1, sizeof(recipe_t));
	registry_free_recipe(r);

	version_list_t *v = safe_calloc(1, sizeof(version_list_t));
	v->versions       = safe_calloc(1, sizeof(char *));
	v->count          = 1;
	registry_free_versions(v);
	PASS();
}

TEST(cov_br_coffee_home_dir_empty)
{
	sds old_home = env_save("COFFEE_HOME");
	setenv("COFFEE_HOME", "", 1);
	ASSERT(strcmp(coffee_home_dir(), "") == 0, "empty COFFEE_HOME is used verbatim");
	env_restore("COFFEE_HOME", old_home);
	PASS();
}

TEST(cov_br_toolcheck_all_tools_present)
{
	ASSERT(toolcheck_run(nullptr) == 0, "nullptr command needs nothing");

	sds root = new_tmpdir();
	ASSERT(root != nullptr, "tmpdir");
	sds         bin     = join_path(root, "bin");
	const char *tools[] = { "clang", "git", "make", "curl", "zstd", "doxygen" };
	make_dirs(bin);
	for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
		sds tool = join_path(bin, tools[i]);
		ASSERT(write_script(tool, "#!/bin/sh\nexit 0\n"), "fake tool");
		sdsfree(tool);
	}
	sds old_path = env_save("PATH");
	setenv("PATH", bin, 1);

	i64 build = toolcheck_run("build");
	i64 fetch = toolcheck_run("fetch");

	env_restore("PATH", old_path);
	ASSERT(build == 0 && fetch == 0, "every tool found");

	sdsfree(bin);
	test_remove_tree(root);
	sdsfree(root);
	PASS();
}

void coffee_register_cov_build_registry_tests(void)
{
	TEST_REGISTER(cov_br_toml_escape_specials);
	TEST_REGISTER(cov_br_toml_escape_manifest_roundtrip);
	TEST_REGISTER(cov_br_safe_fopen_nofollow);
#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
	TEST_REGISTER(cov_br_safe_fopen_nofollow_fdopen_failure);
#endif
	TEST_REGISTER(cov_br_project_find_manifest_cwd_removed);
	TEST_REGISTER(cov_br_project_find_manifest_walks_up);
	TEST_REGISTER(cov_br_project_load_manifest_and_name);
	TEST_REGISTER(cov_br_pkg_dir_resolution_order);
	TEST_REGISTER(cov_br_create_dir_and_file);
	TEST_REGISTER(cov_br_resolve_dep_version);
	TEST_REGISTER(cov_br_run_command_verbose_and_signal);
	TEST_REGISTER(cov_br_run_command_capture_variants);
	TEST_REGISTER(cov_br_run_command_capture_survives_eintr);
	TEST_REGISTER(cov_br_run_command_capture_read_error);
	TEST_REGISTER(cov_br_run_command_fork_failure);
	TEST_REGISTER(cov_br_run_command_capture_pipe_failure);
	TEST_REGISTER(cov_br_compile_sources_no_files);
	TEST_REGISTER(cov_br_compile_sources_builds_argv);
	TEST_REGISTER(cov_br_flag_tokens_trailing_spaces);
	TEST_REGISTER(cov_br_url_scp_style_bad_host);
	TEST_REGISTER(cov_br_dep_constraint_picks_highest_satisfying);
	TEST_REGISTER(cov_br_dep_constraint_local_vendor_and_missing);
	TEST_REGISTER(cov_br_dep_constraint_unreadable_global_dir);
	TEST_REGISTER(cov_br_dep_add_flags_collects_sources);
	TEST_REGISTER(cov_br_build_project_legacy_verbose);
	TEST_REGISTER(cov_br_build_project_make_failure);
	TEST_REGISTER(cov_br_build_project_unparseable_manifest);
	TEST_REGISTER(cov_br_build_project_makefile_not_writable);
	TEST_REGISTER(cov_br_coffee_home_dir_fallbacks);
	TEST_REGISTER(cov_br_registry_search_cached_index);
	TEST_REGISTER(cov_br_registry_search_unusable_index);
	TEST_REGISTER(cov_br_registry_search_download_failure);
	TEST_REGISTER(cov_br_registry_search_refreshes_stale_index);
	TEST_REGISTER(cov_br_registry_search_short_read);
	TEST_REGISTER(cov_br_registry_search_rejects_decompression_bomb);
	TEST_REGISTER(cov_br_registry_get_all_fields);
	TEST_REGISTER(cov_br_registry_get_versions_variants);
	TEST_REGISTER(cov_br_registry_fetch_downloads_recipe);
	TEST_REGISTER(cov_br_registry_fetch_failures);
	TEST_REGISTER(cov_br_registry_source_url_cases);
	TEST_REGISTER(cov_br_validators_every_byte);
	TEST_REGISTER(cov_br_validators_edges);
	TEST_REGISTER(cov_br_url_is_valid_forms);
	TEST_REGISTER(cov_br_run_command_exec_failure_redirects);
	TEST_REGISTER(cov_br_dep_constraint_empty_means_flat);
	TEST_REGISTER(cov_br_dep_add_flags_non_string_entries);
	TEST_REGISTER(cov_br_build_project_legacy_feature_flags);
	TEST_REGISTER(cov_br_build_project_legacy_no_features_default_target);
	TEST_REGISTER(cov_br_build_project_non_legacy_makefile_value);
	TEST_REGISTER(cov_br_build_project_offline_missing_locked_dep);
	TEST_REGISTER(cov_br_registry_index_odd_entries);
	TEST_REGISTER(cov_br_registry_search_oversized_cached_index);
	TEST_REGISTER(cov_br_registry_search_decompressor_failures);
	TEST_REGISTER(cov_br_registry_default_url_and_broken_recipe);
	TEST_REGISTER(cov_br_registry_local_path_registries);
	TEST_REGISTER(cov_br_registry_source_url_without_flag);
	TEST_REGISTER(cov_br_registry_free_sparse_records);
	TEST_REGISTER(cov_br_coffee_home_dir_empty);
	TEST_REGISTER(cov_br_toolcheck_all_tools_present);
}
