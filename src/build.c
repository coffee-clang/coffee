#include "build.h"

#include "lockfile.h"
#include "manifest.h"
#include "registry.h"
#include "strings.h"

#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <sds/sds.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <toml.h>
#include <unistd.h>

static i64 run_command(char **argv, bool verbose)
{
	if (verbose) {
		printf("Running:");
		for (char *const *a = argv; *a; a++) {
			printf(" %s", *a);
		}
		printf("\n");
	}

	pid_t pid = fork();

	if (pid == 0) {
		execvp(argv[0], argv);
		perror("execvp");
		exit(1);
	} else if (pid > 0) {
		i64 wstatus;
		waitpid(pid, (int *)&wstatus, 0);
		if (WIFEXITED(wstatus)) {
			return WEXITSTATUS(wstatus);
		}
		if (verbose) {
			fprintf_safe(stderr, "Command terminated abnormally (signal %d)\n", WTERMSIG(wstatus));
		}
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

	const char *home = getenv("HOME");
	if (home == nullptr) {
		home = "/tmp";
	}
	sds global = sdscatfmt(sdsnew(home), "/.coffee/deps/%s", name);
	if (access(global, F_OK) == 0) {
		return global;
	}
	sdsfree(global);

	return nullptr;
}

/*
 * Append compiler flags for a single dependency to the flags string.
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
							free(s);
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
							free(s);
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

	char *mkdir_argv[] = { (char *)"mkdir", (char *)"-p", (char *)output_dir, nullptr };
	i64   ret          = run_command(mkdir_argv, verbose);
	if (ret != 0) {
		return 1;
	}

	const char *name = manifest->package.name;

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
			free((void *)dflags);
		}
	}

	/* Resolve dependencies: add -I/-L/-l flags and collect dep source files */
	size_t dep_src_cap  = 64;
	size_t dep_src_cnt  = 0;
	sds   *dep_src_list = (sds *)malloc(sizeof(sds) * dep_src_cap);

	for (size_t i = 0; i < manifest->package.dependencies_count; i++) {
		sds dep_name = dep_parse_name(manifest->package.dependencies[i]);

		/* Use lockfile path if available, otherwise resolve from filesystem */
		sds dep_dir = nullptr;
		if (lockfile != nullptr) {
			lockfile_dep_t *locked = lockfile_find_dep(lockfile, dep_name);
			if (locked != nullptr && locked->path != nullptr) {
				dep_dir = sdsnew(locked->path);
			}
		}
		if (dep_dir == nullptr) {
			dep_dir = dep_resolve_dir(dep_name);
		}

		if (dep_dir != nullptr) {
			dep_add_flags(dep_dir, dep_name, &flags, dep_src_list, &dep_src_cnt);
			if (dep_src_cnt + 8 > dep_src_cap) {
				dep_src_cap *= 2;
				dep_src_list = (sds *)realloc(dep_src_list, sizeof(sds) * dep_src_cap);
			}
			sdsfree(dep_dir);
		}
		sdsfree(dep_name);
	}

	glob_t globbuf;
	ret = glob("src/*.c", 0, nullptr, &globbuf);
	if (ret != 0 && dep_src_cnt == 0) {
		fprintf_safe(stderr, "Error: No source files found in src/*.c\n");
		sdsfree(flags);
		free(dep_src_list);
		lockfile_free(lockfile);
		sdsfree(lockfile_path);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	sds outpath = sdscatprintf(sdsempty(), "%s/%s", output_dir, name);
	if (outpath == nullptr) {
		sdsfree(flags);
		globfree(&globbuf);
		free(dep_src_list);
		lockfile_free(lockfile);
		sdsfree(lockfile_path);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	i64 max_tokens = 1;
	for (const char *p = flags; *p != '\0'; p++) {
		if (*p == ' ') {
			max_tokens++;
		}
	}

	size_t argc_total    = (size_t)(1 + max_tokens + 2) + globbuf.gl_pathc + dep_src_cnt + 1;
	char **compiler_argv = (char **)malloc(sizeof(char *) * (argc_total + 1));
	if (compiler_argv == nullptr) {
		sdsfree(flags);
		globfree(&globbuf);
		free(dep_src_list);
		lockfile_free(lockfile);
		sdsfree(lockfile_path);
		if (resolved) {
			features_free(resolved);
		}
		return 1;
	}

	i64 idx              = 0;
	compiler_argv[idx++] = (char *)cc;

	sds   flags_copy = sdsdup(flags);
	char *saveptr;
	char *token = strtok_r(flags_copy, " ", &saveptr);
	while (token) {
		compiler_argv[idx++] = token;
		token                = strtok_r(nullptr, " ", &saveptr);
	}

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

	ret = run_command(compiler_argv, verbose);

	sdsfree(outpath);
	sdsfree(flags_copy);
	free((void *)compiler_argv);
	globfree(&globbuf);
	for (size_t i = 0; i < dep_src_cnt; i++) {
		sdsfree(dep_src_list[i]);
	}
	free(dep_src_list);

	lockfile_free(lockfile);
	sdsfree(lockfile_path);

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
	char **run_argv = (char **)malloc(sizeof(char *) * (size_t)total);
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
	ret = run_command(run_argv, verbose);
	sdsfree(exe_path);
	free((void *)run_argv);
	return ret;
}
