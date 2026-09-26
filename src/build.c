#include "build.h"

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
		if (flags & RUN_CMD_QUIET) {
			int devnull = safe_open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDERR_FILENO);
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
 * Append compiler flags for a single dependency to the flags string.
=======
 * Returns the number of .c source files found (appended to src_argv).
 */
size_t dep_add_flags(const char *dep_dir, const char *dep_name, sds *flags, sds *src_list, size_t *src_count)
{
	size_t found = 0;

	if (dep_dir == nullptr) {
		return 0;
	}

	/* Read library.toml if present */
	sds   toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
	FILE *fp        = safe_fopen(toml_path, "r");
	if (fp) {
		char          errbuf[256];
		toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
		if (conf) {
			/* Include directories */
			toml_array_t *inc = toml_array_in(conf, "include");
			if (inc) {
				i64 n = toml_array_nelem(inc);
				for (i64 i = 0; i < n; i++) {
					toml_raw_t raw = toml_raw_at(inc, i);
					if (raw) {
						char *s;
						if (toml_rtos(raw, &s) == 0 && s) {
							*flags = sdscatprintf(*flags, " -I\"%s/%s\"", dep_dir, s);
							safe_free(s);
							found = 1;
						}
					}
				}
			}

			/* Library directories */
			toml_array_t *lib = toml_array_in(conf, "lib");
			if (lib) {
				i64 n = toml_array_nelem(lib);
				for (i64 i = 0; i < n; i++) {
					toml_raw_t raw = toml_raw_at(lib, i);
					if (raw) {
						char *s;
						if (toml_rtos(raw, &s) == 0 && s) {
							*flags = sdscatprintf(*flags, " -L\"%s/%s\"", dep_dir, s);
							safe_free(s);
							found = 1;
						}
					}
				}
			}

			toml_free(conf);
		}
		safe_fclose(fp);
	}
	sdsfree(toml_path);

	/* Fallback: include/ directory */
	if (!found) {
		sds inc_path = sdscatprintf(sdsempty(), "%s/include", dep_dir);
		if (safe_access(inc_path, F_OK) == 0) {
			*flags = sdscatprintf(*flags, " -I\"%s\"", inc_path);
		}
		sdsfree(inc_path);
	}

	/* Fallback: lib/ directory */
	sds lib_path = sdscatprintf(sdsempty(), "%s/lib", dep_dir);
	if (safe_access(lib_path, F_OK) == 0) {
		*flags = sdscatprintf(*flags, " -L\"%s\"", lib_path);
	}
	sdsfree(lib_path);

	/* Always add -l<name> */
	*flags = sdscatprintf(*flags, " -l\"%s\"", dep_name);

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

i64 compile_sources(sds *src_files, size_t n, sds output, char *cc, const char *flags_in, bool verbose)
{
	if (n == 0) {
		fprintf_safe(stderr, "Error: No source files to compile\n");
		return 1;
	}

	size_t flag_tokens = count_flag_tokens(flags_in);
	size_t argc_total  = 1 + flag_tokens + 2 + n + 1;
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
	argv[idx] = nullptr;

	i64 ret = run_command(argv, (int)verbose ? RUN_CMD_VERBOSE : 0);

	sdsfree(flags_copy);
	safe_free((void *)argv);

	return ret;
}
