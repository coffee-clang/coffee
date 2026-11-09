#include "build.h"

#include "dep_graph.h"
#include "lockfile.h"
#include "makefile.h"
#include "project.h"
#include "registry.h"
#include "safe.h"
#include "strings.h"
#include "version.h"

#include <stdbool.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <sds/sds.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <toml.h>
#include <unistd.h>

i64 run_command(char **argv, int flags)
{
	if (flags & RUN_CMD_VERBOSE) {
		printf_safe("Running:");
		for (char *const *a = argv; *a; a++) {
			printf_safe(" %s", *a);
		}
		printf_safe("\n");
	}

	pid_t pid = fork();

	if (pid == 0) {
		if (flags & (RUN_CMD_QUIET | RUN_CMD_NOOUT)) {
			int devnull = safe_open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				if (flags & RUN_CMD_QUIET) {
					dup2(devnull, STDERR_FILENO);
				}
				if (flags & RUN_CMD_NOOUT) {
					dup2(devnull, STDOUT_FILENO);
				}
				safe_close(devnull);
			}
		}

		execvp(argv[0], argv);

		perror("execvp");
		exit(1);
	}
	if (pid > 0) {
		int wstatus;
		waitpid(pid, &wstatus, 0);
		if (WIFEXITED(wstatus)) {
			return WEXITSTATUS(wstatus);
		}
		fprintf_safe(stderr, "Command terminated abnormally (signal %d)\n", WTERMSIG(wstatus));
		return 1;
	}

	perror("fork");
	return 1;
}

sds run_command_capture(char **argv, int flags)
{
	if (flags & RUN_CMD_VERBOSE) {
		printf_safe("Running:");
		for (char *const *a = argv; *a; a++) {
			printf_safe(" %s", *a);
		}
		printf_safe("\n");
	}

	int pipefd[2];
	if (safe_pipe(pipefd) != 0) {
		return nullptr;
	}

	pid_t pid = fork();

	if (pid == 0) {
		safe_close(pipefd[0]);
		if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
			_exit(1);
		}
		safe_close(pipefd[1]);

		if (flags & RUN_CMD_QUIET) {
			int devnull = safe_open("/dev/null", O_WRONLY);
			if (devnull < 0) {
				_exit(1);
			}
			if (dup2(devnull, STDERR_FILENO) < 0) {
				_exit(1);
			}
			safe_close(devnull);
		}

		execvp(argv[0], argv);
		_exit(1);
	}
	if (pid > 0) {
		safe_close(pipefd[1]);

		sds     result = sdsempty();
		char    buf[4'096];
		ssize_t n;

		while ((n = read(pipefd[0], buf, sizeof(buf))) != 0) {
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				break;
			}
			result = sdscatlen(result, buf, (size_t)n);
		}
		safe_close(pipefd[0]);

		int wstatus;
		waitpid(pid, &wstatus, 0);

		if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
			sdsfree(result);
			return nullptr;
		}

		return result;
	}

	safe_close(pipefd[0]);
	safe_close(pipefd[1]);
	perror("fork");
	return nullptr;
}

/*
 * Resolve a dependency directory: check local deps/<name>/, vendor/<name>/,
 * then global ~/.coffee/deps/<name>/.  Returns the path (caller frees) or
 * nullptr if not found.
 */
sds dep_resolve_dir(const char *name)
{
	sds local = sdscatprintf(sdsempty(), "deps/%s", name);
	if (safe_access(local, F_OK) == 0) {
		return local;
	}
	sdsfree(local);

	sds vendor_dir = sdscatprintf(sdsempty(), "vendor/%s", name);
	if (safe_access(vendor_dir, F_OK) == 0) {
		return vendor_dir;
	}
	sdsfree(vendor_dir);

	sds global = sdscatfmt(sdsempty(), "%s/deps/%s", coffee_home_dir(), name);
	if (safe_access(global, F_OK) == 0) {
		return global;
	}
	sdsfree(global);

	return nullptr;
}

i64 dep_link(const char *name, const char *target)
{
	sds         dep_dir = sdscatprintf(sdsempty(), "deps/%s", name);
	struct stat st;
	if (lstat(dep_dir, &st) == 0) {
		char *rm_argv[] = { "rm", "-rf", dep_dir, nullptr };
		run_command(rm_argv, 0);
	}
	i64 ret = 0;
	if (symlink(target, dep_dir) != 0) {
		fprintf_safe(stderr, "  Error: cannot create symlink '%s' -> '%s'\n", dep_dir, target);
		ret = 1;
	}
	sdsfree(dep_dir);
	return ret;
}

/*
 * Resolve a dependency directory with version constraint.
 * If constraint is null, empty, or "*", behaves like dep_resolve_dir().
 * Otherwise, searches ~/.coffee/deps/<name>/ for versioned subdirectories,
 * filters by constraint, and returns the highest satisfying version path.
 * Falls back to the flat (non-versioned) path if no versioned match is found.
 * Returns the path (caller frees) or nullptr if not found.
 */
sds dep_resolve_dir_constraint(const char *name, const char *constraint)
{
	/* No constraint — fall back to flat resolution */
	if (constraint == nullptr || constraint[0] == '\0' || constraint[0] == '*') {
		return dep_resolve_dir(name);
	}

	/* First check local and vendor paths (flat) */
	sds local = sdscatprintf(sdsempty(), "deps/%s", name);
	if (safe_access(local, F_OK) == 0) {
		return local;
	}
	sdsfree(local);

	sds vendor_dir = sdscatprintf(sdsempty(), "vendor/%s", name);
	if (safe_access(vendor_dir, F_OK) == 0) {
		return vendor_dir;
	}
	sdsfree(vendor_dir);

	/* Check global versioned directory */
	sds  global_base = sdscatfmt(sdsempty(), "%s/deps/%s", coffee_home_dir(), name);
	DIR *dir         = opendir(global_base);
	if (dir == nullptr) {
		/* No versioned directory — try flat global path */
		sdsfree(global_base);
		sds global_flat = sdscatfmt(sdsempty(), "%s/deps/%s", coffee_home_dir(), name);
		if (safe_access(global_flat, F_OK) == 0) {
			return global_flat;
		}
		sdsfree(global_flat);
		return nullptr;
	}

	/* Enumerate versioned subdirectories, find best match */
	sds       best_path   = nullptr;
	version_t best_ver    = { 0, 0, 0 };
	bool      found_match = false;

	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
		if (entry->d_name[0] == '.') {
			continue;
		}

		sds sub_path = sdscatprintf(sdsempty(), "%s/%s", global_base, entry->d_name);

		/* Read version from library.toml */
		sds   lt_path = sdscatprintf(sdsempty(), "%s/library.toml", sub_path);
		FILE *fp      = safe_fopen(lt_path, "r");
		sdsfree(lt_path);

		if (fp == nullptr) {
			sdsfree(sub_path);
			continue;
		}

		char          errbuf[256];
		toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
		safe_fclose(fp);

		if (conf == nullptr) {
			sdsfree(sub_path);
			continue;
		}

		toml_table_t *pkg = toml_table_in(conf, "package");
		if (pkg == nullptr) {
			toml_free(conf);
			sdsfree(sub_path);
			continue;
		}

		toml_datum_t ver = toml_string_in(pkg, "version");
		if (!ver.ok) {
			toml_free(conf);
			sdsfree(sub_path);
			continue;
		}

		/* Check if this version satisfies the constraint */
		if (!version_satisfies(ver.u.s, constraint)) {
			safe_free(ver.u.s);
			toml_free(conf);
			sdsfree(sub_path);
			continue;
		}

		/* Parse version for comparison */
		version_t parsed;
		bool      ok = version_parse(ver.u.s, &parsed);
		safe_free(ver.u.s);
		toml_free(conf);

		if (!ok) {
			sdsfree(sub_path);
			continue;
		}

		/* Keep the highest version */
		if (!found_match || version_cmp(&parsed, &best_ver) > 0) {
			sdsfree(best_path);
			best_path   = sub_path;
			best_ver    = parsed;
			found_match = true;
		} else {
			sdsfree(sub_path);
		}
	}
	closedir(dir);
	sdsfree(global_base);

	if (found_match) {
		return best_path;
	}

	/* Fall back to flat global path */
	sds global_flat = sdscatfmt(sdsempty(), "%s/deps/%s", coffee_home_dir(), name);
	if (safe_access(global_flat, F_OK) == 0) {
		return global_flat;
	}
	sdsfree(global_flat);
	return nullptr;
}

/*
 * Append ` <flag>"<dep_dir>/<path>"` for every string in the library.toml
 * array `key`.  Non-string elements are skipped.  Returns the number of
 * flags appended.
 */
static size_t lt_add_dir_array(toml_table_t *conf, const char *key, const char *flag, const char *dep_dir, sds *flags)
{
	toml_array_t *arr   = toml_array_in(conf, key);
	size_t        added = 0;
	for (i64 i = 0; arr != nullptr && i < toml_array_nelem(arr); i++) {
		toml_datum_t s = toml_string_at(arr, i);
		if (s.ok) {
			*flags = sdscatprintf(*flags, " %s\"%s/%s\"", flag, dep_dir, s.u.s);
			safe_free(s.u.s);
			added++;
		}
	}
	return added;
}

/* Same as lt_add_dir_array() for a single string field (include_dir, lib_dir). */
static size_t lt_add_dir_string(toml_table_t *conf, const char *key, const char *flag, const char *dep_dir, sds *flags)
{
	toml_datum_t s = toml_string_in(conf, key);
	if (!s.ok) {
		return 0;
	}
	*flags = sdscatprintf(*flags, " %s\"%s/%s\"", flag, dep_dir, s.u.s);
	safe_free(s.u.s);
	return 1;
}

/* Append ` <flag>"<dep_dir>/<sub>"` when that directory exists.  Returns 1 if appended. */
static size_t add_dir_if_present(const char *dep_dir, const char *sub, const char *flag, sds *flags)
{
	sds    path  = sdscatprintf(sdsempty(), "%s/%s", dep_dir, sub);
	size_t added = 0;
	if (safe_access(path, F_OK) == 0) {
		*flags = sdscatprintf(*flags, " %s\"%s\"", flag, path);
		added  = 1;
	}
	sdsfree(path);
	return added;
}

/*
 * Append -l for the library.toml libname, a string or an array of
 * strings.  Returns the number of flags appended.
 */
static size_t lt_add_libnames(toml_table_t *conf, sds *flags)
{
	toml_datum_t one = toml_string_in(conf, "libname");
	if (one.ok) {
		*flags = sdscatprintf(*flags, " -l\"%s\"", one.u.s);
		safe_free(one.u.s);
		return 1;
	}
	toml_array_t *arr   = toml_array_in(conf, "libname");
	size_t        added = 0;
	for (i64 i = 0; arr != nullptr && i < toml_array_nelem(arr); i++) {
		toml_datum_t s = toml_string_at(arr, i);
		if (s.ok) {
			*flags = sdscatprintf(*flags, " -l\"%s\"", s.u.s);
			safe_free(s.u.s);
			added++;
		}
	}
	return added;
}

static toml_table_t *lt_parse(const char *dep_dir)
{
	sds   toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
	FILE *fp        = safe_fopen(toml_path, "r");
	sdsfree(toml_path);
	if (fp == nullptr) {
		return nullptr;
	}
	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);
	return conf;
}

/*
 * Append compiler and linker flags for a single dependency to the flags
 * string.  Returns nonzero when library.toml named include or library
 * directories.
 *
 * Include directories come from library.toml `include` (array) and
 * `include_dir` (string), else from include/ when it exists.  Library
 * directories come from `lib` and `lib_dir`, else from lib/.  The two
 * fallbacks are independent.  Libraries are linked by `libname` (a string
 * or an array of strings); a package without libname that has a library
 * directory is linked as -l<dep_name>.  `header_only = true` links
 * nothing.
 */
size_t dep_add_flags(const char *dep_dir, const char *dep_name, sds *flags, sds *src_list, size_t *src_count)
{
	if (dep_dir == nullptr) {
		return 0;
	}

	toml_table_t *conf        = lt_parse(dep_dir);
	size_t        inc_found   = 0;
	size_t        lib_found   = 0;
	size_t        libnames    = 0;
	bool          header_only = false;
	if (conf != nullptr) {
		inc_found += lt_add_dir_array(conf, "include", "-I", dep_dir, flags);
		inc_found += lt_add_dir_string(conf, "include_dir", "-I", dep_dir, flags);
	}
	size_t found = inc_found;
	if (inc_found == 0) {
		add_dir_if_present(dep_dir, "include", "-I", flags);
	}

	if (conf != nullptr) {
		lib_found += lt_add_dir_array(conf, "lib", "-L", dep_dir, flags);
		lib_found += lt_add_dir_string(conf, "lib_dir", "-L", dep_dir, flags);
	}
	found += lib_found;
	if (lib_found == 0) {
		lib_found = add_dir_if_present(dep_dir, "lib", "-L", flags);
	}

	if (conf != nullptr) {
		toml_datum_t ho = toml_bool_in(conf, "header_only");
		header_only     = ho.ok && ho.u.b;
		if (!header_only) {
			libnames = lt_add_libnames(conf, flags);
		}
		toml_free(conf);
	}
	if (!header_only && libnames == 0 && lib_found > 0) {
		*flags = sdscatprintf(*flags, " -l\"%s\"", dep_name);
	}

	/* Also compile dep source files if they exist (only if src_list provided) */
	if (src_list != nullptr && src_count != nullptr) {
		glob_t dep_glob;
		sds    dep_src_glob = sdscatprintf(sdsempty(), "%s/src/*.c", dep_dir);
		if (glob(dep_src_glob, 0, nullptr, &dep_glob) == 0) {
			for (size_t i = 0; i < dep_glob.gl_pathc; i++) {
				src_list[*src_count] = sdsnew(dep_glob.gl_pathv[i]);
				(*src_count)++;
			}
			globfree(&dep_glob);
		}
		sdsfree(dep_src_glob);
	}

	return found;
}

/*
 * Extract dependency name from a raw TOML dependency entry string.
 * Handles "name", "name = ...", and "name = { ... }" formats.
 * Returns a new sds with the bare name (caller frees).
 */
sds dep_parse_name(const char *entry)
{
	sds         name;
	const char *eq = strchr(entry, '=');
	if (eq) {
		size_t len = (size_t)(eq - entry);
		while (len > 0 && entry[len - 1] == ' ') {
			len--;
		}
		name = sdsnewlen(entry, len);
	} else {
		name = sdsnew(entry);
	}
	return name;
}

bool dep_name_is_valid(const char *name)
{
	if (name == nullptr || name[0] == '\0') {
		return false;
	}
	/* A dependency name is used both as a path component and as a TOML
	 * bare key, so only the bare-key charset is accepted.  This rejects
	 * '.', '..', '/', '"', whitespace and every other TOML
	 * metacharacter.  Ranges are spelled out instead of using isalnum()
	 * to stay locale-independent. */
	for (const char *p = name; *p != '\0'; p++) {
		bool ok =
		    (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-';
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool build_target_is_valid(const char *target)
{
	if (target == nullptr || target[0] == '\0' || target[0] == '-') {
		return false;
	}
	for (const char *p = target; *p != '\0'; p++) {
		char c = *p;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
		      c == '-')) {
			return false;
		}
	}
	return true;
}

/*
 * Validate a version string before it is used in a filesystem path.
 * Version strings can come from third-party content (a cloned
 * library.toml), so they must not be able to escape the directory they
 * are interpolated into.  Deliberately stricter than version_parse():
 * rejects '/', '\', '"', whitespace and shell metacharacters, plus a
 * leading '.' (covers ".", "..", ".hidden") or '-'.
 */
bool version_is_valid(const char *version)
{
	if (version == nullptr || version[0] == '\0') {
		return false;
	}
	if (version[0] == '.' || version[0] == '-') {
		return false;
	}
	for (size_t i = 0; version[i] != '\0'; i++) {
		char c = version[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
		      c == '+' || c == '~' || c == '*' || c == '-')) {
			return false;
		}
	}
	return true;
}

/*
 * Validate a git ref (branch/tag/rev) before passing it to git.
 * Rejects anything that could be parsed as a git option (leading '-')
 * or that contains shell/option metacharacters.  Allows refs/heads/...,
 * origin/..., tags, and short SHAs.
 */
bool ref_is_valid(const char *ref)
{
	if (ref == nullptr || ref[0] == '\0') {
		return false;
	}
	/* A leading '-' would be parsed by git as an option. */
	if (ref[0] == '-') {
		return false;
	}
	for (size_t i = 0; ref[i] != '\0'; i++) {
		char c = ref[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
		      c == '/' || c == '-')) {
			return false;
		}
	}
	return true;
}

/*
 * True when a ref looks like a full git object id: at least 7 hex
 * characters (case-insensitive).  Such refs are revisions and must be
 * checked out directly — `clone --branch` only accepts branches and
 * tags, and a shallow fetch may not contain the object.  Assumes
 * ref_is_valid() has already accepted the ref; a hex-looking branch name
 * routed through the rev path still resolves via git's rev-parse object
 * lookup (both hex and the abbreviation resolve to the same object).
 */
bool ref_is_rev(const char *ref)
{
	if (ref == nullptr) {
		return false;
	}
	size_t len = strlen(ref);
	if (len < 7) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		char c = ref[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
			return false;
		}
	}
	return true;
}

/*
 * Lowercase a revision ref (hex object ids are case-insensitive) so
 * callers can compare it against `rev-parse` output, which is always
 * lowercase.  Returns a new sds the caller must free; returns nullptr
 * for nullptr.
 */
sds ref_lowercase(const char *ref)
{
	if (ref == nullptr) {
		return nullptr;
	}
	sds out = sdsnew(ref);
	for (size_t i = 0; i < sdslen(out); i++) {
		out[i] = (char)tolower((unsigned char)out[i]);
	}
	return out;
}

/*
 * Validate a git clone URL.  Allowed: the schemes https, git, ssh,
 * git+ssh and git+https, local filesystem paths, and scp-style ssh
 * (user@host:path).  The http scheme is deliberately rejected.  Anything
 * that could be interpreted as a git option (leading '-') is rejected.
 */
bool url_is_valid(const char *url)
{
	if (url == nullptr) {
		return false;
	}
	if (strncmp(url, "https://", 8) == 0 || strncmp(url, "git://", 6) == 0 || strncmp(url, "ssh://", 6) == 0 ||
	    strncmp(url, "git+ssh://", 10) == 0 || strncmp(url, "git+https://", 12) == 0) {
		return true;
	}
	/* Local filesystem paths (git clone supports them).  These cannot be
	 * parsed as git options because they start with '/' or '.', and the
	 * '--' separator in the argv provides the real protection. */
	if (url[0] == '/' || strncmp(url, "./", 2) == 0 || strncmp(url, "../", 3) == 0) {
		return true;
	}
	/* Any other scheme ('http', 'ftp', 'file', ...) is not on the
	 * allow-list; reject it here so a scheme-bearing URL can only pass
	 * through the whitelist above, never via the scp-style test below
	 * (e.g. http://user@host:8080/x contains '@' and ':'). */
	if (strstr(url, "://") != nullptr) {
		return false;
	}
	/* scp-style syntax: user@host:path (e.g. git@github.com:org/repo.git).
	 * user/host are [A-Za-z0-9._-]+ with exactly one '@' separator, path
	 * is [A-Za-z0-9._/~+-]+ and must not start with '-' (git would parse
	 * it as an option).  IPv6 scp form (git@[addr]:path) is rejected. */
	const char *at = strchr(url, '@');
	if (at != nullptr && at > url && at[1] != '\0') {
		const char *colon = strchr(at, ':');
		if (colon != nullptr && colon[1] != '\0' && colon[1] != '-') {
			/* user and host are validated separately so the single '@'
			 * separator is the only one allowed. */
			bool ok = true;
			for (const char *p = url; p < at && ok; p++) {
				char c = *p;
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
				      c == '_' || c == '-')) {
					ok = false;
				}
			}
			for (const char *p = at + 1; p < colon && ok; p++) {
				char c = *p;
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
				      c == '_' || c == '-')) {
					ok = false;
				}
			}
			for (const char *p = colon + 1; *p != '\0' && ok; p++) {
				char c = *p;
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
				      c == '/' || c == '_' || c == '-' || c == '~' || c == '+')) {
					ok = false;
				}
			}
			if (ok) {
				return true;
			}
		}
	}
	return false;
}

bool url_is_valid_remote(const char *url)
{
	if (url == nullptr || !url_is_valid(url)) {
		return false;
	}
	if (url[0] == '/' || strncmp(url, "./", 2) == 0 || strncmp(url, "../", 3) == 0) {
		return false;
	}
	return true;
}

size_t count_flag_tokens(const char *flags)
{
	if (flags == nullptr || flags[0] == '\0') {
		return 0;
	}
	size_t      count = 0;
	const char *p     = flags;
	while (*p != '\0') {
		while (*p == ' ') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		count++;
		bool in_quotes = false;
		while (*p != '\0') {
			if (*p == '"') {
				in_quotes = !in_quotes;
			} else if (*p == ' ' && !in_quotes) {
				break;
			}
			p++;
		}
	}
	return count;
}

sds split_flags_to_argv(const char *flags, char **argv, size_t start_idx, size_t *end_idx)
{
	if (flags == nullptr || flags[0] == '\0') {
		*end_idx = start_idx;
		return sdsempty();
	}
	sds   copy = sdsnew(flags);
	char *p    = copy;
	while (*p != '\0') {
		while (*p == ' ') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		argv[start_idx++] = p;
		/* A token is delimited by spaces outside quotes; double quotes
		 * are stripped and mark regions where spaces are literal, so
		 * -l"foo bar" yields the single argv element "-lfoo bar". */
		bool in_quotes = false;
		while (*p != '\0') {
			if (*p == '"') {
				memmove(p, p + 1, strlen(p));
				in_quotes = !in_quotes;
				continue;
			}
			if (*p == ' ' && !in_quotes) {
				break;
			}
			p++;
		}
		if (*p == ' ') {
			*p = '\0';
			p++;
		}
	}
	*end_idx = start_idx;
	return copy;
}

void dep_split_flags(const char *flags, bool has_sources, sds *compile, sds *link)
{
	size_t ntok   = count_flag_tokens(flags);
	char **tokens = (char **)safe_malloc(sizeof(char *) * (ntok + 1));
	size_t end    = 0;
	sds    copy   = split_flags_to_argv(flags, tokens, 0, &end);
	for (size_t i = 0; i < end; i++) {
		bool is_link = strncmp(tokens[i], "-L", 2) == 0 || strncmp(tokens[i], "-l", 2) == 0;
		if (!is_link) {
			*compile = sdscatprintf(*compile, " \"%s\"", tokens[i]);
		} else if (!has_sources) {
			*link = sdscatprintf(*link, " \"%s\"", tokens[i]);
		}
	}
	sdsfree(copy);
	safe_free((void *)tokens);
}

sds flags_append_unquoted(sds out, const char *flags)
{
	size_t ntok   = count_flag_tokens(flags);
	char **tokens = (char **)safe_malloc(sizeof(char *) * (ntok + 1));
	size_t end    = 0;
	sds    copy   = split_flags_to_argv(flags, tokens, 0, &end);
	for (size_t i = 0; i < end; i++) {
		out = sdscatprintf(out, "%s ", tokens[i]);
	}
	sdsfree(copy);
	safe_free((void *)tokens);
	return out;
}

size_t append_clang_tidy_extra_args(const char *flags, char **argv, size_t start_idx)
{
	size_t tokens_cnt = count_flag_tokens(flags);
	char **tokens     = (char **)safe_malloc(sizeof(char *) * (tokens_cnt + 1));

	size_t end_idx;
	sds    flags_copy = split_flags_to_argv(flags, tokens, 0, &end_idx);

	for (size_t i = 0; i < end_idx; i++) {
		argv[start_idx++] = sdscatprintf(sdsnew("--extra-arg="), "%s", tokens[i]);
	}

	sdsfree(flags_copy);
	safe_free((void *)tokens);
	return start_idx;
}

i64 compile_sources(sds *src_files, size_t n, sds output, char *cc, const char *flags_in, const char *link_flags,
                    bool verbose)
{
	if (n == 0) {
		fprintf_safe(stderr, "Error: No source files to compile\n");
		return 1;
	}

	size_t flag_tokens = count_flag_tokens(flags_in);
	size_t link_tokens = count_flag_tokens(link_flags);
	size_t argc_total  = 1 + flag_tokens + 2 + n + link_tokens + 1;
	char **argv        = (char **)safe_malloc(sizeof(char *) * argc_total);

	size_t idx  = 0;
	argv[idx++] = cc;

	size_t end_idx;
	sds    flags_copy = split_flags_to_argv(flags_in, argv, idx, &end_idx);
	idx               = end_idx;

	argv[idx++] = (char *)"-o";
	argv[idx++] = output;

	for (size_t i = 0; i < n; i++) {
		argv[idx++] = src_files[i];
	}
	sds link_copy = split_flags_to_argv(link_flags, argv, idx, &end_idx);
	idx           = end_idx;
	argv[idx]     = nullptr;

	i64 ret = run_command(argv, (int)verbose ? RUN_CMD_VERBOSE : 0);

	sdsfree(link_copy);
	sdsfree(flags_copy);
	safe_free((void *)argv);

	return ret;
}

/*
 * Build a project: generate (or reuse) the Makefile and run make on the
 * requested target.  Shared by `coffee build` and `coffee run`.
 *
 * The Makefile is generated from the manifest, the resolved dependency
 * graph and the resolved features, and is written only when its content
 * changes.  A `[compilation] Makefile = "legacy"` manifest opts out of
 * generation: the existing Makefile is run as-is, with RELEASE/DEBUG and
 * CFLAGS_EXTRA passed through for Makefiles that key off them.
 */
/* Options the parser accepts but no handler acts on yet.  Rejecting them
 * here is the honest alternative to silently building the default target
 * and reporting success. */
static bool build_has_unimplemented_flag(const options *opts)
{
	return opts->bin != nullptr || opts->example != nullptr || opts->profile != nullptr || opts->target != nullptr;
}

static const char *build_unimplemented_flag(const options *opts)
{
	if (opts->bin != nullptr) {
		return "--bin";
	}
	if (opts->example != nullptr) {
		return "--example";
	}
	if (opts->profile != nullptr) {
		return "--profile";
	}
	return "--target";
}

i64 deps_fetch_missing(manifest_t *m, options *opts)
{
	lockfile_t  *lf = lockfile_parse("Coffee.lock");
	dep_graph_t *g  = dep_graph_create(m, lf, true);
	lockfile_free(lf);
	if (g == nullptr) {
		/* makefile_generate reports the graph error. */
		return 0;
	}
	sds missing = nullptr;
	for (size_t i = 1; i < g->count && missing == nullptr; i++) {
		if (g->nodes[i].path == nullptr || safe_access(g->nodes[i].path, F_OK) != 0) {
			missing = sdsnew(g->nodes[i].name);
		}
	}
	dep_graph_free(g);
	if (missing == nullptr) {
		return 0;
	}
	i64 ret = 1;
	if (opts->offline) {
		fprintf_safe(stderr, "Error: dependency '%s' is not fetched, and --offline prevents fetching it\n", missing);
	} else {
		ret = handle_fetch(opts);
	}
	sdsfree(missing);
	return ret;
}

i64 build_project(options *opts, const char *target, sds *exe_path_out)
{
	if (build_has_unimplemented_flag(opts)) {
		fprintf_safe(stderr, "Error: %s is not yet implemented\n", build_unimplemented_flag(opts));
		return 1;
	}

	/* Find the manifest */
	char *manifest_path = nullptr;
	if (opts->manifest_path) {
		manifest_path = sdsnew(opts->manifest_path);
	} else {
		manifest_path = project_find_manifest(nullptr);
	}
	if (manifest_path == nullptr) {
		fprintf_safe(stderr, "Error: Could not find Coffee.toml in current directory\n");
		return 1;
	}

	/* Extract project directory from manifest path */
	char *dir_end = strrchr(manifest_path, '/');
	sds   project_dir;
	if (dir_end) {
		project_dir = sdsnewlen(manifest_path, (size_t)(dir_end - manifest_path));
	} else {
		project_dir = sdsnew(".");
	}

	manifest_t *manifest = manifest_parse(manifest_path);
	sdsfree(manifest_path);
	if (manifest == nullptr) {
		fprintf_safe(stderr, "Error: Could not parse Coffee.toml\n");
		sdsfree(project_dir);
		return 1;
	}

	/* Resolve the executable path against the project directory, in both
	 * generated and legacy mode, so `coffee run` can exec it after the
	 * cwd has been restored.  The directory goes through the same
	 * resolution the renderer uses, so a --target-dir the Makefile would
	 * reject does not produce a phantom "Executable not found". */
	if (exe_path_out != nullptr) {
		*exe_path_out = sdscatprintf(sdsempty(), "%s/%s/%s", project_dir, makefile_resolve_target_dir(opts->target_dir),
		                             manifest->package.name);
	}

	/* Dependency resolution and make both operate relative to the
	 * project directory, so move there for the duration of the build.
	 * The old cwd is kept as a descriptor rather than a getcwd() path:
	 * a deleted cwd or one longer than PATH_MAX must not skip the move. */
	int  old_cwd = -1;
	bool moved   = false;
	if (strcmp(project_dir, ".") != 0) {
		old_cwd = safe_open(".", O_RDONLY | O_DIRECTORY);
		if (chdir(project_dir) != 0) {
			fprintf_safe(stderr, "Error: cannot enter project directory '%s'\n", project_dir);
			if (old_cwd >= 0) {
				safe_close(old_cwd);
			}
			manifest_free(manifest);
			sdsfree(project_dir);
			return 1;
		}
		moved = true;
	}

	/* Resolve features once; used by both the generated Makefile and
	 * the legacy CFLAGS_EXTRA overlay. */
	sds   *requested       = nullptr;
	size_t requested_count = 0;
	if (opts->features) {
		features_parse_cli(opts->features, &requested, &requested_count);
	}
	resolved_features_t *rf = nullptr;
	if (requested_count > 0 || (i64)opts->all_features) {
		rf = features_resolve(manifest, requested_count > 0 ? requested : nullptr, requested_count, opts->all_features,
		                      opts->no_default_features);
	}
	for (size_t i = 0; i < requested_count; i++) {
		sdsfree(requested[i]);
	}
	safe_free(requested);

	bool legacy = manifest->compilation.makefile != nullptr && strcmp(manifest->compilation.makefile, "legacy") == 0;

	/* Run make.  Worst case in legacy mode: make, -C, dir, -jN,
	 * RELEASE=1, DEBUG=1, CFLAGS_EXTRA=..., target, then the nullptr
	 * terminator — 9 slots; 10 leaves one slot of slack. */
	char  *make_argv[10];
	size_t make_argc    = 0;
	sds    jflag        = nullptr;
	sds    cflags_extra = nullptr;

	i64 ret = 1;
	if (strcmp(target, "clean") != 0 && deps_fetch_missing(manifest, opts) != 0) {
		goto done;
	}
	if (legacy) {
		/* Run the existing Makefile */
		if (safe_access("Makefile", F_OK) != 0) {
			fprintf_safe(stderr, "Error: No Makefile found. Coffee requires a Makefile for building.\n");
			fprintf_safe(stderr, "Use 'coffee new <name>' to create a new project with a Makefile template.\n");
			goto done;
		}
	} else {
		/* Generate the Makefile.  `clean` uses none of the values an
		 * unsafe-value warning is about, so stay quiet for it. */
		bool warn = strcmp(target, "clean") != 0;
		if (makefile_generate(manifest, project_dir, rf, opts, warn) != 0) {
			fprintf_safe(stderr, "Error: Could not generate Makefile\n");
			goto done;
		}
	}

	make_argv[make_argc++] = "make";
	make_argv[make_argc++] = "-C";
	make_argv[make_argc++] = unconst(moved ? "." : project_dir);

	if (opts->jobs > 0) {
		jflag                  = sdscatprintf(sdsempty(), "-j%lld", (long long)opts->jobs);
		make_argv[make_argc++] = jflag;
	}

	if (legacy) {
		/* Legacy Makefiles may key off these variables */
		if ((i64)opts->release) {
			make_argv[make_argc++] = "RELEASE=1";
		}
		if ((i64)opts->debug) {
			make_argv[make_argc++] = "DEBUG=1";
		}
		if (rf != nullptr) {
			size_t dflags_count = 0;
			sds   *dflags       = features_to_compiler_flags(rf, manifest->package.name, &dflags_count);
			if (dflags_count > 0) {
				cflags_extra = sdsnew("CFLAGS_EXTRA=");
				for (size_t i = 0; i < dflags_count; i++) {
					cflags_extra = sdscatprintf(cflags_extra, "%s%s", dflags[i], (i + 1 < dflags_count) ? " " : "");
				}
				make_argv[make_argc++] = cflags_extra;
			}
			for (size_t i = 0; i < dflags_count; i++) {
				sdsfree(dflags[i]);
			}
			safe_free(dflags);
		}
	}

	if (target != nullptr && target[0] != '\0') {
		make_argv[make_argc++] = unconst(target);
	}
	make_argv[make_argc] = nullptr;

	if (opts->verbose) {
		printf_safe("Running:");
		for (size_t i = 0; i < make_argc; i++) {
			printf_safe(" %s", make_argv[i]);
		}
		printf_safe("\n");
	}

	ret = run_command(make_argv, 0);
	sdsfree(jflag);
	sdsfree(cflags_extra);

	if (ret != 0) {
		fprintf_safe(stderr, "Build failed (make exited with code %d)\n", (int)ret);
	}

done:
	features_free(rf);
	manifest_free(manifest);
	if (moved && old_cwd >= 0) {
		fchdir(old_cwd);
	}
	if (old_cwd >= 0) {
		safe_close(old_cwd);
	}
	sdsfree(project_dir);
	return ret;
}
