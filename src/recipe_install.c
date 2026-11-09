#include "recipe_install.h"

#include "build.h"
#include "project.h"
#include "registry.h"
#include "safe.h"
#include "toolcheck.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>
#include <unistd.h>

/* Guards the recursive install of recipe dependencies against cycles. */
#define RECIPE_MAX_DEPTH 32

/* Lines of install.log shown when an install fails. */
#define RECIPE_LOG_TAIL "20"

/* The sha256 of a file as 64 lowercase hex digits, or nullptr. */
static sds recipe_sha256(const char *path)
{
	char *argv[] = { "sha256sum", "--", unconst(path), nullptr };
	sds   out    = run_command_capture(argv, RUN_CMD_QUIET);
	if (out == nullptr) {
		return nullptr;
	}
	if (sdslen(out) < 64) {
		sdsfree(out);
		return nullptr;
	}
	sdsrange(out, 0, 63);
	return out;
}

/* Read a top-level string key from a TOML file, or nullptr. */
static sds recipe_toml_string(const char *path, const char *key)
{
	FILE *fp = safe_fopen(path, "r");
	if (fp == nullptr) {
		return nullptr;
	}
	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);
	if (conf == nullptr) {
		return nullptr;
	}
	sds          result = nullptr;
	toml_datum_t v      = toml_string_in(conf, key);
	if (v.ok) {
		result = sdsnew(v.u.s);
		safe_free(v.u.s);
	}
	toml_free(conf);
	return result;
}

/* The checksum recorded in <prefix>/.coffee-installed, or nullptr. */
static sds recipe_stamp_checksum(const char *prefix)
{
	sds stamp    = sdscatprintf(sdsempty(), "%s/.coffee-installed", prefix);
	sds checksum = recipe_toml_string(stamp, "checksum");
	sdsfree(stamp);
	return checksum;
}

static void recipe_info_fill(struct recipe_info *info, const char *prefix, const char *version, const char *checksum)
{
	info->prefix   = sdsnew(prefix);
	info->version  = sdsnew(version);
	info->checksum = sdsnew(checksum);
}

/*
 * Run install.sh with COFFEE_HOME set.  Unless verbose, its output goes to
 * log, and the tail of the log is shown on failure.
 */
static i64 recipe_run_script(const char *name, const char *script, const char *log, bool verbose)
{
	sds home_env = sdscatprintf(sdsempty(), "COFFEE_HOME=%s", coffee_home_dir());
	i64 ret;
	/* Keep our progress lines ahead of the script's output. */
	fflush(stdout);
	if (verbose) {
		char *argv[] = { "env", home_env, "bash", unconst(script), nullptr };
		ret          = run_command(argv, 0);
	} else {
		char *argv[] = {
			"env", home_env, "bash", "-c", "exec bash \"$0\" >\"$1\" 2>&1", unconst(script), unconst(log), nullptr,
		};
		ret = run_command(argv, 0);
	}
	sdsfree(home_env);
	if (ret == 0) {
		return 0;
	}
	fprintf_safe(stderr, "  Error: install.sh for '%s' failed\n", name);
	if (!verbose) {
		char *tail_argv[] = { "tail", "-n", RECIPE_LOG_TAIL, "--", unconst(log), nullptr };
		sds   tail        = run_command_capture(tail_argv, RUN_CMD_QUIET);
		if (tail != nullptr) {
			fprintf_safe(stderr, "  Last lines of %s:\n%s", log, tail);
			sdsfree(tail);
		}
	}
	return ret;
}

static enum recipe_status recipe_install_at(const char *name, const char *constraint, const lockfile_dep_t *locked,
                                            bool verbose, struct recipe_info *info, int depth);

/*
 * Install the dependencies listed in the [dependencies] table of a
 * downloaded recipe.  Returns 0 on success.
 */
static i64 recipe_install_deps(const char *name, const char *recipe_toml, bool verbose, int depth)
{
	FILE *fp = safe_fopen(recipe_toml, "r");
	if (fp == nullptr) {
		return 0;
	}
	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);
	if (conf == nullptr) {
		fprintf_safe(stderr, "  Error: cannot parse the recipe for '%s': %s\n", name, errbuf);
		return 1;
	}
	i64           ret  = 0;
	toml_table_t *deps = toml_table_in(conf, "dependencies");
	for (i64 i = 0; deps != nullptr && ret == 0; i++) {
		const char *dep = toml_key_in(deps, i);
		if (dep == nullptr) {
			break;
		}
		toml_datum_t       constraint = toml_string_in(deps, dep);
		struct recipe_info dep_info;
		enum recipe_status st =
		    recipe_install_at(dep, constraint.ok ? constraint.u.s : nullptr, nullptr, verbose, &dep_info, depth + 1);
		if (constraint.ok) {
			safe_free(constraint.u.s);
		}
		recipe_info_free(&dep_info);
		if (st == RECIPE_NOT_FOUND) {
			fprintf_safe(stderr, "  Error: '%s' depends on '%s', which is not in the registry\n", name, dep);
			ret = 1;
		} else if (st == RECIPE_FAILED) {
			ret = 1;
		}
	}
	toml_free(conf);
	return ret;
}

/*
 * Check that the host tools in the [build-dependencies] table of a
 * downloaded recipe (cmake, meson, pkg-config, ...) are on PATH.  They
 * are needed by install.sh and are never installed from the registry.
 * Returns 0 when every tool is present.
 */
static i64 recipe_check_build_tools(const char *name, const char *recipe_toml)
{
	FILE *fp = safe_fopen(recipe_toml, "r");
	if (fp == nullptr) {
		return 0;
	}
	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);
	if (conf == nullptr) {
		fprintf_safe(stderr, "  Error: cannot parse the recipe for '%s': %s\n", name, errbuf);
		return 1;
	}
	i64           ret   = 0;
	toml_table_t *tools = toml_table_in(conf, "build-dependencies");
	for (i64 i = 0; tools != nullptr; i++) {
		const char *tool = toml_key_in(tools, i);
		if (tool == nullptr) {
			break;
		}
		if (!toolcheck_on_path(tool)) {
			fprintf_safe(stderr, "  Error: building '%s' needs '%s', which is not on PATH\n", name, tool);
			ret = 1;
		}
	}
	toml_free(conf);
	return ret;
}

/* Check the downloaded recipe against the constraint and the lockfile. */
static bool recipe_matches(const char *name, const char *version, const char *checksum, const char *constraint,
                           const lockfile_dep_t *locked)
{
	if (!version_satisfies(version, constraint)) {
		fprintf_safe(stderr, "  Error: '%s' %s is required, but the registry has version %s\n", name, constraint,
		             version);
		return false;
	}
	if (locked == nullptr) {
		return true;
	}
	if (locked->version != nullptr && strcmp(locked->version, version) != 0) {
		fprintf_safe(stderr, "  Error: Coffee.lock pins '%s' %s, but the registry has version %s\n", name,
		             locked->version, version);
		return false;
	}
	if (locked->checksum != nullptr && strcmp(locked->checksum, checksum) != 0) {
		fprintf_safe(stderr, "  Error: install.sh for '%s' changed since Coffee.lock was written\n", name);
		return false;
	}
	return true;
}

/*
 * Run the downloaded install.sh unless prefix already holds an install
 * made by the same script, then copy the recipe into the prefix and write
 * the stamp.  Returns 0 on success.
 */
static i64 recipe_install_prefix(const char *name, const char *recipe_dir, const char *prefix, const char *checksum,
                                 bool verbose)
{
	sds installed = recipe_stamp_checksum(prefix);
	if (installed != nullptr && strcmp(installed, checksum) == 0) {
		sdsfree(installed);
		return 0;
	}
	sdsfree(installed);

	sds recipe_toml = sdscatprintf(sdsempty(), "%s/library.toml", recipe_dir);
	i64 tools_ret   = recipe_check_build_tools(name, recipe_toml);
	sdsfree(recipe_toml);
	if (tools_ret != 0) {
		return tools_ret;
	}

	if (verbose) {
		printf_safe("    Installing %s into %s...\n", name, prefix);
	}
	/* A prefix without a matching stamp is a failed or outdated install. */
	char *rm_argv[] = { "rm", "-rf", unconst(prefix), nullptr };
	run_command(rm_argv, 0);

	sds script = sdscatprintf(sdsempty(), "%s/install.sh", recipe_dir);
	sds log    = sdscatprintf(sdsempty(), "%s/install.log", recipe_dir);
	i64 ret    = recipe_run_script(name, script, log, verbose);
	sdsfree(log);
	sdsfree(script);
	if (ret != 0) {
		return ret;
	}
	if (safe_access(prefix, F_OK) != 0) {
		fprintf_safe(stderr, "  Error: install.sh for '%s' did not create '%s'\n", name, prefix);
		return 1;
	}

	sds   toml_src  = sdscatprintf(sdsempty(), "%s/library.toml", recipe_dir);
	sds   toml_dst  = sdscatprintf(sdsempty(), "%s/library.toml", prefix);
	char *cp_argv[] = { "cp", "--", toml_src, toml_dst, nullptr };
	ret             = run_command(cp_argv, 0);
	sdsfree(toml_dst);
	sdsfree(toml_src);
	if (ret != 0) {
		fprintf_safe(stderr, "  Error: cannot copy the recipe for '%s' into '%s'\n", name, prefix);
		return ret;
	}

	sds stamp   = sdscatprintf(sdsempty(), "%s/.coffee-installed", prefix);
	sds content = sdscatprintf(sdsempty(), "checksum = \"%s\"\n", checksum);
	ret         = create_file(stamp, content);
	if (ret != 0) {
		fprintf_safe(stderr, "  Error: cannot write '%s'\n", stamp);
	}
	sdsfree(content);
	sdsfree(stamp);
	return ret;
}

static enum recipe_status recipe_install_at(const char *name, const char *constraint, const lockfile_dep_t *locked,
                                            bool verbose, struct recipe_info *info, int depth)
{
	memset(info, 0, sizeof(*info));
	if (depth > RECIPE_MAX_DEPTH) {
		fprintf_safe(stderr, "  Error: the dependencies of '%s' are nested too deeply (cycle?)\n", name);
		return RECIPE_FAILED;
	}
	if (!dep_name_is_valid(name)) {
		fprintf_safe(stderr, "  Error: invalid dependency name '%s'\n", name);
		return RECIPE_FAILED;
	}

	sds       key = sdsnew(name);
	recipe_t *r   = registry_get(key);
	if (r == nullptr) {
		sdsfree(key);
		return RECIPE_NOT_FOUND;
	}

	const char *home       = coffee_home_dir();
	sds         recipe_dir = sdscatprintf(sdsempty(), "%s/recipes/%s", home, name);
	/* Drop a stale install.sh from an earlier recipe revision. */
	char       *rm_argv[] = { "rm", "-rf", recipe_dir, nullptr };
	run_command(rm_argv, 0);

	enum recipe_status status      = RECIPE_FAILED;
	sds                script      = sdscatprintf(sdsempty(), "%s/install.sh", recipe_dir);
	sds                recipe_toml = sdscatprintf(sdsempty(), "%s/library.toml", recipe_dir);
	sds                checksum    = nullptr;
	sds                prefix      = nullptr;
	if (registry_fetch(key, r->version, recipe_dir) != 0) {
		status = RECIPE_NOT_FOUND;
	} else if (safe_access(script, F_OK) != 0) {
		/* The caller clones recipe_url at the recipe version instead. */
		if (!version_is_valid(r->version)) {
			fprintf_safe(stderr, "  Error: recipe for '%s' has an invalid version\n", name);
		} else if (recipe_matches(name, r->version, "", constraint, nullptr)) {
			info->version = sdsnew(r->version);
			status        = RECIPE_NO_SCRIPT;
		}
	} else if (!version_is_valid(r->version)) {
		fprintf_safe(stderr, "  Error: recipe for '%s' has an invalid version\n", name);
	} else {
		checksum = recipe_sha256(script);
		prefix   = sdscatprintf(sdsempty(), "%s/%s/%s", home, name, r->version);
		if (checksum == nullptr) {
			fprintf_safe(stderr, "  Error: cannot compute the checksum of '%s'\n", script);
		} else if (recipe_matches(name, r->version, checksum, constraint, locked) &&
		           recipe_install_deps(name, recipe_toml, verbose, depth) == 0 &&
		           recipe_install_prefix(name, recipe_dir, prefix, checksum, verbose) == 0) {
			recipe_info_fill(info, prefix, r->version, checksum);
			status = RECIPE_INSTALLED;
		}
	}

	sdsfree(prefix);
	sdsfree(checksum);
	sdsfree(recipe_toml);
	sdsfree(script);
	sdsfree(recipe_dir);
	registry_free_recipe(r);
	sdsfree(key);
	return status;
}

enum recipe_status recipe_install(const char *name, const char *constraint, const lockfile_dep_t *locked, bool verbose,
                                  struct recipe_info *info)
{
	return recipe_install_at(name, constraint, locked, verbose, info, 0);
}

bool recipe_read_installed(const char *dir, struct recipe_info *info)
{
	memset(info, 0, sizeof(*info));
	char *resolved = realpath(dir, nullptr);
	if (resolved == nullptr) {
		return false;
	}
	sds checksum = recipe_stamp_checksum(resolved);
	sds toml     = sdscatprintf(sdsempty(), "%s/library.toml", resolved);
	sds version  = recipe_toml_string(toml, "version");
	sdsfree(toml);
	bool ok = checksum != nullptr && version != nullptr;
	if (ok) {
		recipe_info_fill(info, resolved, version, checksum);
	}
	sdsfree(version);
	sdsfree(checksum);
	safe_free(resolved);
	return ok;
}

void recipe_info_free(struct recipe_info *info)
{
	sdsfree(info->prefix);
	sdsfree(info->version);
	sdsfree(info->checksum);
	memset(info, 0, sizeof(*info));
}
