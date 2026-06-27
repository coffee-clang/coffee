#include "build.h"

#include "dep_graph.h"
#include "lockfile.h"
#include "manifest.h"
#include "registry.h"
#include "safe.h"
#include "strings.h"
#include "version.h"

#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDERR_FILENO);
				close(devnull);
			}
		}

		execvp(argv[0], argv);

		perror("execvp");
		exit(1);
	} else if (pid > 0) {
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

/*
 * Resolve a dependency directory: check local deps/<name>/, vendor/<name>/,
 * then global ~/.coffee/deps/<name>/.  Returns the path (caller frees) or
 * nullptr if not found.
 */
sds dep_resolve_dir(const char *name)
{
	sds local = sdscatprintf(sdsempty(), "deps/%s", name);
	if (access(local, F_OK) == 0) {
		return local;
	}
	sdsfree(local);

	sds vendor_dir = sdscatprintf(sdsempty(), "vendor/%s", name);
	if (access(vendor_dir, F_OK) == 0) {
		return vendor_dir;
	}
	sdsfree(vendor_dir);

	sds global = sdscatfmt(sdsempty(), "%s/deps/%s", coffee_home_dir(), name);
	if (access(global, F_OK) == 0) {
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
	if (access(local, F_OK) == 0) {
		return local;
	}
	sdsfree(local);

	sds vendor_dir = sdscatprintf(sdsempty(), "vendor/%s", name);
	if (access(vendor_dir, F_OK) == 0) {
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
		if (access(global_flat, F_OK) == 0) {
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
		FILE *fp      = fopen(lt_path, "r");
		sdsfree(lt_path);

		if (fp == nullptr) {
			sdsfree(sub_path);
			continue;
		}

		char          errbuf[256];
		toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
		fclose(fp);

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
	if (access(global_flat, F_OK) == 0) {
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
	FILE *fp        = fopen(toml_path, "r");
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
							*flags = sdscatprintf(*flags, " -I%s/%s", dep_dir, s);
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
							*flags = sdscatprintf(*flags, " -L%s/%s", dep_dir, s);
							safe_free(s);
							found = 1;
						}
					}
				}
			}

			toml_free(conf);
		}
		fclose(fp);
	}
	sdsfree(toml_path);

	/* Fallback: include/ directory */
	if (!found) {
		sds inc_path = sdscatprintf(sdsempty(), "%s/include", dep_dir);
		if (access(inc_path, F_OK) == 0) {
			*flags = sdscatprintf(*flags, " -I%s", inc_path);
		}
		sdsfree(inc_path);
	}

	/* Fallback: lib/ directory */
	sds lib_path = sdscatprintf(sdsempty(), "%s/lib", dep_dir);
	if (access(lib_path, F_OK) == 0) {
		*flags = sdscatprintf(*flags, " -L%s", lib_path);
	}
	sdsfree(lib_path);

	/* Always add -l<name> */
	*flags = sdscatprintf(*flags, " -l%s", dep_name);

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
		while (*p != ' ' && *p != '\0') {
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
		while (*p != ' ' && *p != '\0') {
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

i64 build_project(manifest_t *manifest, build_opts_t *opts)
{
	if (manifest == nullptr || manifest->package.name == nullptr) {
		fprintf_safe(stderr, "Error: No valid manifest found\n");
		return 1;
	}

	char *cc         = getenv("CC") != nullptr ? getenv("CC") : "clang";
	char *output_dir = opts != nullptr && opts->target_dir != nullptr ? opts->target_dir : "target/debug";

	bool verbose = false;
	if (opts != nullptr) {
		verbose = opts->verbose;
	}

	char *mkdir_argv[] = { "mkdir", "-p", output_dir, nullptr };
	i64   ret          = run_command(mkdir_argv, (int)verbose ? RUN_CMD_VERBOSE : 0);
	if (ret != 0) {
		return 1;
	}

	sds name = manifest->package.name;

	/* Parse lockfile if it exists */
	sds         lockfile_path = sdsnew("Coffee.lock");
	lockfile_t *lockfile      = lockfile_parse(lockfile_path);

	if (opts != nullptr && opts->locked) {
		if (lockfile == nullptr) {
			fprintf_safe(stderr, "Error: Coffee.lock not found (--locked requires it)\n");
			sdsfree(lockfile_path);
			return 1;
		}
		/* Check staleness: Coffee.toml should not be newer than Coffee.lock */
		struct stat toml_st;
		struct stat lock_st;
		if (stat("Coffee.toml", &toml_st) == 0 && stat("Coffee.lock", &lock_st) == 0) {
			if (toml_st.st_mtime > lock_st.st_mtime) {
				fprintf_safe(stderr,
				             "Error: Coffee.toml is newer than Coffee.lock (--locked requires up-to-date lockfile)\n");
				lockfile_free(lockfile);
				sdsfree(lockfile_path);
				return 1;
			}
		}
	}

	const char *flags_str = "-O0 -g";
	if (opts != nullptr) {
		if (opts->release) {
			flags_str = "-O2 -s";
		} else if (opts->debug) {
			flags_str = "-g";
		}
	}
	sds flags = sdsnew(flags_str);

	resolved_features_t *resolved     = nullptr;
	bool                 has_features = false;
	if (opts != nullptr) {
		if (opts->features_count > 0) {
			has_features = true;
		}
		if (opts->all_features) {
			has_features = true;
		}
	}
	if (has_features) {
		sds *requested = nullptr;
		if (opts->features_count > 0) {
			requested = opts->features;
		}
		resolved =
		    features_resolve(manifest, requested, opts->features_count, opts->all_features, opts->no_default_features);

		if (resolved) {
			size_t dflags_count = 0;
			sds   *dflags       = features_to_compiler_flags(resolved, name, &dflags_count);
			for (size_t i = 0; i < dflags_count; i++) {
				flags = sdscatfmt(flags, " %s", dflags[i]);
				sdsfree(dflags[i]);
			}
			safe_free((void *)dflags);
		}
	}

	/* Resolve dependencies via dep_graph (with caching) */
	dep_graph_t *dep_graph = dep_graph_get(manifest, lockfile, true, ".");
	sds         *dep_names = nullptr;
	size_t       dep_total = 0;
	if (dep_graph != nullptr) {
		dep_names = dep_graph_names(dep_graph, &dep_total);
	}

	size_t dep_src_cnt  = 0;
	size_t dep_src_cap  = 64;
	sds   *dep_src_list = (sds *)safe_malloc(sizeof(sds) * dep_src_cap);

	for (size_t gi = 0; gi < dep_total; gi++) {
		sds dep_name = dep_names[gi];
		if (dep_name == nullptr) {
			continue;
		}
		/* Skip root (at index 0) */
		if (gi == 0 && manifest->package.name != nullptr && strcmp(dep_name, manifest->package.name) == 0) {
			continue;
		}

		/* Add compiler flags */
		sds dep_flags = dep_graph_flags(dep_graph, dep_name);
		flags         = sdscatfmt(flags, " %s", dep_flags);
		sdsfree(dep_flags);

		/* Add source files */
		size_t     src_cnt = 0;
		const sds *srcs    = dep_graph_sources(dep_graph, dep_name, &src_cnt);
		for (size_t j = 0; j < src_cnt; j++) {
			if (dep_src_cnt >= dep_src_cap) {
				dep_src_cap *= 2;
				dep_src_list = (sds *)safe_realloc(dep_src_list, sizeof(sds) * dep_src_cap);
			}
			dep_src_list[dep_src_cnt++] = sdsnew(srcs[j]);
		}
	}

	glob_t globbuf;
	ret = glob("src/*.c", 0, nullptr, &globbuf);
	if (ret != 0 && dep_src_cnt == 0) {
		fprintf_safe(stderr, "Error: No source files found in src/*.c\n");
		sdsfree(flags);
		safe_free(dep_src_list);
		lockfile_free(lockfile);
		sdsfree(lockfile_path);
		dep_graph_free(dep_graph);
		safe_free(dep_names);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	sds outpath = sdscatprintf(sdsempty(), "%s/%s", output_dir, name);
	if (outpath == nullptr) {
		sdsfree(flags);
		globfree(&globbuf);
		safe_free(dep_src_list);
		lockfile_free(lockfile);
		sdsfree(lockfile_path);
		dep_graph_free(dep_graph);
		safe_free(dep_names);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	size_t flag_tokens   = count_flag_tokens(flags);
	size_t argc_total    = (size_t)(1 + flag_tokens + 2) + globbuf.gl_pathc + dep_src_cnt + 1;
	char **compiler_argv = (char **)safe_malloc(sizeof(char *) * (argc_total + 1));

	i64 idx              = 0;
	compiler_argv[idx++] = cc;

	size_t end_idx;
	sds    flags_copy = split_flags_to_argv(flags, compiler_argv, (size_t)idx, &end_idx);
	idx               = (i64)end_idx;

	compiler_argv[idx++] = (char *)"-o";
	compiler_argv[idx++] = outpath;

	for (size_t i = 0; i < globbuf.gl_pathc; i++) {
		compiler_argv[idx++] = globbuf.gl_pathv[i];
	}
	for (size_t i = 0; i < dep_src_cnt; i++) {
		compiler_argv[idx++] = dep_src_list[i];
	}
	compiler_argv[idx] = nullptr;

	sdsfree(flags);

	ret = run_command(compiler_argv, (int)verbose ? RUN_CMD_VERBOSE : 0);

	sdsfree(outpath);
	sdsfree(flags_copy);
	safe_free((void *)compiler_argv);
	globfree(&globbuf);
	for (size_t i = 0; i < dep_src_cnt; i++) {
		sdsfree(dep_src_list[i]);
	}
	safe_free(dep_src_list);

	lockfile_free(lockfile);
	sdsfree(lockfile_path);

	dep_graph_free(dep_graph);
	safe_free(dep_names);

	if (resolved) {
		features_free(resolved);
	}

	return ret;
}

i64 build_run(manifest_t *manifest, build_opts_t *opts, sds *args, i64 argc)
{
	i64 ret = build_project(manifest, opts);
	if (ret != 0) {
		return ret;
	}

	const char *output_dir = opts != nullptr && opts->target_dir != nullptr ? opts->target_dir : "target/debug";
	const char *name       = manifest->package.name;

	sds exe_path = sdscatprintf(sdsempty(), "%s/%s", output_dir, name);
	if (exe_path == nullptr) {
		return 1;
	}

	if (access(exe_path, X_OK) != 0) {
		fprintf_safe(stderr, "Error: Executable not found: %s\n", exe_path);
		sdsfree(exe_path);
		return 1;
	}

	i64    total    = 1 + (args != nullptr ? argc : 0) + 1;
	char **run_argv = (char **)safe_malloc(sizeof(char *) * (size_t)total);
	if (run_argv == nullptr) {
		sdsfree(exe_path);
		return 1;
	}

	i64 idx         = 0;
	run_argv[idx++] = exe_path;
	for (i64 i = 0; i < argc && args; i++) {
		run_argv[idx++] = args[i];
	}
	run_argv[idx] = nullptr;

	bool verbose = false;
	if (opts != nullptr) {
		verbose = opts->verbose;
	}
	ret = run_command(run_argv, (int)verbose ? RUN_CMD_VERBOSE : 0);
	sdsfree(exe_path);
	safe_free((void *)run_argv);
	return ret;
}
