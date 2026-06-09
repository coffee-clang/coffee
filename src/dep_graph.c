#include "dep_graph.h"

#include "build.h"
#include "strings.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <sds/sds.h>
#include <sys/stat.h>
#include <toml.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Extract version constraint from a TOML table entry.
 * The value at key can be:
 *   - a plain string (e.g. ">= 2.0")  -> return it
 *   - an inline table with a "version" field (e.g. { version = ">= 2.0" }) -> return the version
 * Returns an sds the caller must free, or nullptr.
 */
static sds get_toml_constraint(toml_table_t *table, const char *key)
{
	toml_datum_t str_val = toml_string_in(table, key);
	if (str_val.ok) {
		sds result = sdsnew(str_val.u.s);
		free(str_val.u.s);
		return result;
	}

	toml_table_t *inline_tbl = toml_table_in(table, key);
	if (inline_tbl != nullptr) {
		toml_datum_t ver = toml_string_in(inline_tbl, "version");
		if (ver.ok) {
			sds result = sdsnew(ver.u.s);
			free(ver.u.s);
			return result;
		}
	}

	return nullptr;
}

/*
 * Extract version constraint from a raw dependency string (from root manifest).
 * Handles:
 *   "name"                  -> nullptr
 *   "name >= 2.0"           -> ">= 2.0"
 *   "name = \"1.0.0\""      -> "1.0.0"
 *   "name = \">= 2.0\""     -> ">= 2.0"
 *   "name = { ... }"        -> nullptr (inline table)
 * Returns an sds the caller must free, or nullptr.
 */
static sds dep_extract_constraint(const char *entry)
{
	if (entry == nullptr) {
		return nullptr;
	}

	const char *eq = strchr(entry, '=');
	if (eq == nullptr) {
		/* No '=' — try to find inline constraint operator after the name */
		const char *space = entry;
		while (*space != '\0' && *space != ' ' && *space != '\t') {
			space++;
		}
		while (*space == ' ' || *space == '\t') {
			space++;
		}
		if (*space == '>' || *space == '<' || *space == '=' || *space == '!') {
			return sdsnew(space);
		}
		return nullptr;
	}

	/* '=' found — parse part after '=' */
	const char *after_eq = eq + 1;
	while (*after_eq == ' ' || *after_eq == '\t') {
		after_eq++;
	}

	if (*after_eq == '{') {
		return nullptr; /* inline table */
	}

	/* Extract value, stripping quotes */
	if (*after_eq == '"') {
		after_eq++;
		sds result = sdsempty();
		while (*after_eq != '\0' && *after_eq != '"') {
			result = sdscatlen(result, after_eq, 1);
			after_eq++;
		}
		return result;
	}

	/* No quotes — take remaining, trimming trailing whitespace */
	const char *end = after_eq + strlen(after_eq);
	while (end > after_eq && (*(end - 1) == ' ' || *(end - 1) == '\t')) {
		end--;
	}
	return sdsnewlen(after_eq, (size_t)(end - after_eq));
}

static bool node_contains_name(dep_node_t *nodes, size_t n, const char *name)
{
	for (size_t i = 0; i < n; i++) {
		if (nodes[i].name != nullptr && strcmp(nodes[i].name, name) == 0) {
			return true;
		}
	}
	return false;
}

/*
 * Find a dep in the graph by name. Returns -1 if not found.
 */
static i64 find_node(const dep_graph_t *g, const char *name)
{
	if (g == nullptr || name == nullptr) {
		return -1;
	}
	for (size_t i = 0; i < g->count; i++) {
		if (g->nodes[i].name != nullptr && strcmp(g->nodes[i].name, name) == 0) {
			return (i64)i;
		}
	}
	return -1;
}

/*
 * Add a node to the graph. Returns the index of the new node.
 */
static i64 add_node(dep_graph_t *g, const char *name)
{
	if (g->count >= g->capacity) {
		size_t new_cap = g->capacity == 0 ? 32 : g->capacity * 2;
		g->nodes       = realloc(g->nodes, new_cap * sizeof(dep_node_t));
		if (g->nodes == nullptr) {
			return -1;
		}
		g->capacity = new_cap;
	}
	size_t idx = g->count;
	memset(&g->nodes[idx], 0, sizeof(dep_node_t));
	g->nodes[idx].name = sdsnew(name);
	g->count++;
	return (i64)idx;
}

/*
 * Read [dependencies] from a library.toml or Coffee.toml file.
 * Returns the toml_table_t for dependencies (borrowed from conf).
 * The caller must free conf via toml_free().
 */
static toml_table_t *dep_graph_read_table(const char *dep_dir, toml_table_t **out_conf)
{
	*out_conf = nullptr;

	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", dep_dir);
	FILE *fp        = fopen(toml_path, "r");
	if (fp == nullptr) {
		sdsfree(toml_path);
		toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
		fp        = fopen(toml_path, "r");
		if (fp == nullptr) {
			sdsfree(toml_path);
			return nullptr;
		}
	}
	sdsfree(toml_path);

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		return nullptr;
	}

	toml_table_t *deps = toml_table_in(conf, "dependencies");
	if (deps == nullptr) {
		toml_free(conf);
		return nullptr;
	}

	*out_conf = conf;
	return deps;
}

/*
 * Read library.toml version from a dep directory.
 */
static sds read_version(const char *dep_dir)
{
	sds   toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
	FILE *fp        = fopen(toml_path, "r");
	sdsfree(toml_path);
	if (fp == nullptr) {
		return sdsnew("*");
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		return sdsnew("*");
	}

	toml_datum_t ver = toml_string_in(conf, "version");
	sds          result;
	if (ver.ok) {
		result = sdsnew(ver.u.s);
		free(ver.u.s);
	} else {
		result = sdsnew("*");
	}
	toml_free(conf);
	return result;
}

/*
 * Get a git ref from a dep's manifest inline table.
 * Checks tag, branch, rev in that order of precedence.
 */
static sds get_git_ref_from_manifest(const char *dep_dir)
{
	sds   toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", dep_dir);
	FILE *fp        = fopen(toml_path, "r");
	if (fp == nullptr) {
		sdsfree(toml_path);
		toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
		fp        = fopen(toml_path, "r");
		if (fp == nullptr) {
			sdsfree(toml_path);
			return nullptr;
		}
	}
	sdsfree(toml_path);

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		return nullptr;
	}

	toml_table_t *deps = toml_table_in(conf, "dependencies");
	sds           ref  = nullptr;
	if (deps) {
		/* Look through each dep for the inline table */
		for (i64 i = 0;; i++) {
			const char *key = toml_key_in(deps, i);
			if (key == nullptr) {
				break;
			}
			toml_table_t *tbl = toml_table_in(deps, key);
			if (tbl == nullptr) {
				continue;
			}
			toml_datum_t tag = toml_string_in(tbl, "tag");
			if (tag.ok) {
				ref = sdsnew(tag.u.s);
				free(tag.u.s);
				break;
			}
			toml_datum_t branch = toml_string_in(tbl, "branch");
			if (branch.ok) {
				ref = sdsnew(branch.u.s);
				free(branch.u.s);
				break;
			}
			toml_datum_t rev = toml_string_in(tbl, "rev");
			if (rev.ok) {
				ref = sdsnew(rev.u.s);
				free(rev.u.s);
				break;
			}
		}
	}
	toml_free(conf);
	return ref;
}

/*
 * Collect source .c files from a dep's src/ directory.
 */
static void collect_sources(const char *dep_dir, sds **out_src, size_t *out_count)
{
	*out_count = 0;
	*out_src   = nullptr;

	glob_t gbuf;
	sds    pattern = sdscatprintf(sdsempty(), "%s/src/*.c", dep_dir);
	if (glob(pattern, 0, nullptr, &gbuf) == 0) {
		if (gbuf.gl_pathc > 0) {
			*out_src = calloc((size_t)gbuf.gl_pathc, sizeof(sds));
			if (*out_src) {
				for (size_t i = 0; i < (size_t)gbuf.gl_pathc; i++) {
					(*out_src)[i] = sdsnew(gbuf.gl_pathv[i]);
				}
				*out_count = (size_t)gbuf.gl_pathc;
			}
		}
		globfree(&gbuf);
	}
	sdsfree(pattern);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

dep_graph_t *dep_graph_create(manifest_t *m, lockfile_t *lf, bool offline)
{
	if (m == nullptr) {
		return nullptr;
	}

	dep_graph_t *g = calloc(1, sizeof(dep_graph_t));
	if (g == nullptr) {
		return nullptr;
	}
	g->offline = offline;

	/* Add root node */
	i64 root_idx = add_node(g, m->package.name != nullptr ? m->package.name : "root");
	if (root_idx < 0) {
		dep_graph_free(g);
		return nullptr;
	}
	g->nodes[root_idx].path    = sdsnew(".");
	g->nodes[root_idx].version = m->package.version ? sdsnew(m->package.version) : sdsnew("*");
	g->nodes[root_idx].visited = true;

	/* BFS over dependencies */
	size_t dep_cursor = 0;
	while (dep_cursor < g->count) {
		dep_node_t *cur = &g->nodes[dep_cursor];
		dep_cursor++;

		/* Resolve the directory for this dep */
		sds dep_dir = nullptr;
		if (cur->path == nullptr || strcmp(cur->path, ".") == 0) {
			/* Root or unresolved — resolve it */
			if (lf != nullptr) {
				lockfile_dep_t *locked = lockfile_find_dep(lf, cur->name);
				if (locked != nullptr && locked->path != nullptr && locked->path[0] != '\0') {
					dep_dir = sdsnew(locked->path);
				}
			}
			if (dep_dir == nullptr) {
				dep_dir = dep_resolve_dir_constraint(cur->name, cur->version_constraint);
			}
			if (dep_dir != nullptr) {
				sdsfree(cur->path);
				cur->path = sdsnew(dep_dir);
			}
		} else {
			dep_dir = sdsnew(cur->path);
		}

		if (dep_dir != nullptr) {
			/* Read version */
			if (cur->version == nullptr) {
				cur->version = read_version(dep_dir);
			}

			/* Check if git repo */
			sds git_dir = sdscatprintf(sdsempty(), "%s/.git", dep_dir);
			if (access(git_dir, F_OK) == 0) {
				cur->is_git = true;
				/* Get the git ref from the manifest (branch/tag/rev) */
				sds ref = get_git_ref_from_manifest(dep_dir);
				if (ref != nullptr) {
					cur->git_ref = ref;
				}
			}
			sdsfree(git_dir);

			/* Build compiler flags */
			sds flags = sdsempty();
			dep_add_flags(dep_dir, cur->name, &flags, nullptr, nullptr);
			cur->flags = flags;

			/* Collect source files */
			collect_sources(dep_dir, &cur->sources, &cur->src_count);
		}

		/* Read transitive dependencies from dep's manifest */
		if (dep_dir != nullptr) {
			toml_table_t *dep_conf        = nullptr;
			toml_table_t *transitive_deps = dep_graph_read_table(dep_dir, &dep_conf);
			if (transitive_deps) {
				for (i64 i = 0;; i++) {
					const char *dep_key = toml_key_in(transitive_deps, i);
					if (dep_key == nullptr) {
						break;
					}
					/* Skip self-references */
					if (cur->name != nullptr && strcmp(dep_key, cur->name) == 0) {
						continue;
					}
					/* Extract version constraint for this dep */
					sds constraint = get_toml_constraint(transitive_deps, dep_key);

					/* Check if already in graph — validate version constraint */
					i64 existing = find_node(g, dep_key);
					if (existing >= 0) {
						if (constraint != nullptr && g->nodes[existing].version != nullptr) {
							if (!version_satisfies(g->nodes[existing].version, constraint)) {
								fprintf_safe(stderr, "Warning: %s requires %s %s but %s is resolved\n",
								             cur->name != nullptr ? cur->name : "(root)", dep_key, constraint,
								             g->nodes[existing].version);
							}
						}
						sdsfree(constraint);
						continue;
					}
					/* Add as a new node */
					i64 child_idx = add_node(g, dep_key);
					if (child_idx < 0) {
						sdsfree(constraint);
						toml_free(dep_conf);
						sdsfree(dep_dir);
						dep_graph_free(g);
						return nullptr;
					}
					if (constraint != nullptr) {
						g->nodes[child_idx].version_constraint = constraint;
					}
				}
				toml_free(dep_conf);
			}
		}

		/* For root (dep_cursor == 1 after increment), also process root's own deps */
		if (dep_cursor == 1) {
			/* Process root manifest's dependencies */
			for (size_t i = 0; i < m->package.dependencies_count; i++) {
				sds dep_name = dep_parse_name(m->package.dependencies[i]);
				if (dep_name == nullptr) {
					continue;
				}

				/* Extract version constraint from raw string or structured deps */
				sds constraint = nullptr;
				/* Check structured deps for inline table version */
				for (size_t j = 0; j < m->dependencies.deps_count; j++) {
					if (m->dependencies.deps[j].name != nullptr &&
					    strcmp(m->dependencies.deps[j].name, dep_name) == 0) {
						if (m->dependencies.deps[j].version != nullptr) {
							constraint = sdsnew(m->dependencies.deps[j].version);
						}
						break;
					}
				}
				if (constraint == nullptr) {
					constraint = dep_extract_constraint(m->package.dependencies[i]);
				}

				/* Check if already in graph — validate version constraint */
				i64 existing = find_node(g, dep_name);
				if (existing >= 0) {
					if (constraint != nullptr && g->nodes[existing].version != nullptr) {
						if (!version_satisfies(g->nodes[existing].version, constraint)) {
							fprintf_safe(stderr, "Warning: %s requires %s %s but %s is resolved\n",
							             m->package.name != nullptr ? m->package.name : "(root)", dep_name, constraint,
							             g->nodes[existing].version);
						}
					}
					sdsfree(constraint);
					sdsfree(dep_name);
					continue;
				}

				i64 child_idx = add_node(g, dep_name);
				if (child_idx < 0) {
					sdsfree(constraint);
					sdsfree(dep_name);
					dep_graph_free(g);
					return nullptr;
				}
				if (constraint != nullptr) {
					g->nodes[child_idx].version_constraint = constraint;
				}
				sdsfree(dep_name);
			}
		}

		sdsfree(dep_dir);
	}

	return g;
}

void dep_graph_free(dep_graph_t *g)
{
	if (g == nullptr) {
		return;
	}
	for (size_t i = 0; i < g->count; i++) {
		sdsfree(g->nodes[i].name);
		sdsfree(g->nodes[i].path);
		sdsfree(g->nodes[i].version);
		sdsfree(g->nodes[i].version_constraint);
		sdsfree(g->nodes[i].commit);
		sdsfree(g->nodes[i].git_ref);
		sdsfree(g->nodes[i].flags);
		for (size_t j = 0; j < g->nodes[i].src_count; j++) {
			sdsfree(g->nodes[i].sources[j]);
		}
		free(g->nodes[i].sources);
	}
	free(g->nodes);
	free(g);
}

size_t dep_graph_count(const dep_graph_t *g)
{
	return g != nullptr ? g->count : 0;
}

const char *dep_graph_node_name(const dep_graph_t *g, size_t index)
{
	if (g == nullptr || index >= g->count) {
		return nullptr;
	}
	return g->nodes[index].name;
}

sds *dep_graph_names(const dep_graph_t *g, size_t *count)
{
	if (g == nullptr || count == nullptr) {
		return nullptr;
	}
	/* Build a flat array of name sds pointers */
	sds *names = calloc(g->count, sizeof(sds));
	if (names == nullptr) {
		*count = 0;
		return nullptr;
	}
	for (size_t i = 0; i < g->count; i++) {
		names[i] = g->nodes[i].name;
	}
	*count = g->count;
	return names;
}

sds dep_graph_flags(const dep_graph_t *g, const char *dep_name)
{
	if (g == nullptr || dep_name == nullptr) {
		return sdsempty();
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0 || g->nodes[idx].flags == nullptr) {
		return sdsempty();
	}
	return sdsnew(g->nodes[idx].flags);
}

const sds *dep_graph_sources(const dep_graph_t *g, const char *dep_name, size_t *count)
{
	if (g == nullptr || dep_name == nullptr || count == nullptr) {
		return nullptr;
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0) {
		*count = 0;
		return nullptr;
	}
	*count = g->nodes[idx].src_count;
	return (const sds *)g->nodes[idx].sources;
}

const char *dep_graph_path(const dep_graph_t *g, const char *dep_name)
{
	if (g == nullptr || dep_name == nullptr) {
		return nullptr;
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0) {
		return nullptr;
	}
	return g->nodes[idx].path;
}

const char *dep_graph_commit(const dep_graph_t *g, const char *dep_name)
{
	if (g == nullptr || dep_name == nullptr) {
		return nullptr;
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0) {
		return nullptr;
	}
	return g->nodes[idx].commit;
}

bool dep_graph_is_git(const dep_graph_t *g, const char *dep_name)
{
	if (g == nullptr || dep_name == nullptr) {
		return false;
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0) {
		return false;
	}
	return g->nodes[idx].is_git;
}

const char *dep_graph_git_ref(const dep_graph_t *g, const char *dep_name)
{
	if (g == nullptr || dep_name == nullptr) {
		return nullptr;
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0) {
		return nullptr;
	}
	return g->nodes[idx].git_ref;
}

i64 dep_graph_compare_remote(const dep_graph_t *g, const char *dep_name, sds *behind_by)
{
	if (g == nullptr || dep_name == nullptr) {
		return -1;
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0 || !g->nodes[idx].is_git || g->nodes[idx].path == nullptr) {
		return -1;
	}

	const char *dep_path = g->nodes[idx].path;
	const char *ref      = g->nodes[idx].git_ref != nullptr ? g->nodes[idx].git_ref : "origin/HEAD";

	if (g->offline) {
		if (behind_by) {
			*behind_by = sdsnew("(offline)");
		}
		return -1;
	}

	/* git fetch origin */
	sds cmd = sdscatprintf(sdsempty(), "cd '%s' && git fetch origin --depth 1 2>/dev/null", dep_path);
	i64 ret = system(cmd);
	sdsfree(cmd);
	if (ret != 0) {
		if (behind_by) {
			*behind_by = sdsnew("(fetch failed)");
		}
		return -1;
	}

	/* git rev-list --count HEAD..<ref>  */
	sds   rev_cmd = sdscatprintf(sdsempty(), "cd '%s' && git rev-list --count HEAD..%s 2>/dev/null", dep_path, ref);
	FILE *pipe    = popen(rev_cmd, "r");
	sdsfree(rev_cmd);
	if (pipe == nullptr) {
		if (behind_by) {
			*behind_by = sdsnew("(rev-list failed)");
		}
		return -1;
	}

	char buf[64] = { 0 };
	if (fgets(buf, sizeof(buf), pipe) == nullptr) {
		pclose(pipe);
		if (behind_by) {
			*behind_by = sdsnew("(parse failed)");
		}
		return -1;
	}
	pclose(pipe);

	i64 behind = atol(buf);
	if (behind_by) {
		if (behind == 0) {
			*behind_by = sdsnew("up-to-date");
		} else if (behind == 1) {
			*behind_by = sdsnew("1 commit behind");
		} else {
			*behind_by = sdscatprintf(sdsempty(), "%lld commits behind", (long long)behind);
		}
	}
	return behind;
}

i64 dep_graph_fetch_git(dep_graph_t *g, const char *dep_name, bool verbose)
{
	if (g == nullptr || dep_name == nullptr) {
		return -1;
	}
	i64 idx = find_node(g, dep_name);
	if (idx < 0 || !g->nodes[idx].is_git) {
		return -1;
	}

	const char *dep_path = g->nodes[idx].path;
	const char *ref      = g->nodes[idx].git_ref;

	if (dep_path == nullptr) {
		return -1;
	}

	sds cmd;
	if (ref != nullptr) {
		/* Check out the configured ref */
		if (verbose) {
			printf_safe("    Fetching %s (%s)...\n", dep_name, ref);
		}
		cmd = sdscatprintf(sdsempty(),
		                   "cd '%s' && git fetch origin --depth 1 '%s' 2>/dev/null && git checkout '%s' 2>/dev/null",
		                   dep_path, ref, ref);
	} else {
		if (verbose) {
			printf_safe("    Fetching %s...\n", dep_name);
		}
		cmd = sdscatprintf(
		    sdsempty(), "cd '%s' && git fetch --depth 1 origin 2>/dev/null && git reset --hard origin/HEAD 2>/dev/null",
		    dep_path);
	}

	i64 ret = system(cmd);
	sdsfree(cmd);

	if (ret == 0) {
		/* Update commit SHA in the node */
		sds   rev_cmd = sdscatprintf(sdsempty(), "cd '%s' && git rev-parse HEAD 2>/dev/null", dep_path);
		FILE *pipe    = popen(rev_cmd, "r");
		sdsfree(rev_cmd);
		if (pipe != nullptr) {
			char rev_buf[128] = { 0 };
			if (fgets(rev_buf, sizeof(rev_buf), pipe) != nullptr) {
				size_t len = strlen(rev_buf);
				if (len > 0 && rev_buf[len - 1] == '\n') {
					rev_buf[len - 1] = '\0';
				}
				if (rev_buf[0] != '\0') {
					sdsfree(g->nodes[idx].commit);
					g->nodes[idx].commit = sdsnew(rev_buf);
				}
			}
			pclose(pipe);
		}
	}
	return ret;
}

/* ------------------------------------------------------------------ */
/* Graph cache — read / write to .coffee/build-cache/                   */
/* ------------------------------------------------------------------ */

static sds toml_escape(const char *s)
{
	if (s == nullptr) {
		return sdsempty();
	}
	sds out = sdsempty();
	for (const char *p = s; *p != '\0'; p++) {
		if (*p == '\\') {
			out = sdscatlen(out, "\\\\", 2);
		} else if (*p == '"') {
			out = sdscatlen(out, "\\\"", 2);
		} else if (*p == '\n') {
			out = sdscatlen(out, "\\n", 2);
		} else if (*p == '\r') {
			out = sdscatlen(out, "\\r", 2);
		} else if (*p == '\t') {
			out = sdscatlen(out, "\\t", 2);
		} else {
			out = sdscatlen(out, p, 1);
		}
	}
	return out;
}

static i64 dep_graph_cache_write(const dep_graph_t *g, const char *cache_path, i64 toml_mtime, i64 lock_mtime)
{
	FILE *fp = fopen(cache_path, "w");
	if (fp == nullptr) {
		return -1;
	}

	fprintf_safe(fp, "# This file is automatically generated by coffee.\n");
	fprintf_safe(fp, "# It caches the resolved dependency graph for faster builds.\n");
	fprintf_safe(fp, "[metadata]\n");
	fprintf_safe(fp, "coffee_toml_mtime = %lld\n", (long long)toml_mtime);
	fprintf_safe(fp, "coffee_lock_mtime = %lld\n", (long long)lock_mtime);
	fprintf_safe(fp, "cache_version = 1\n");
	fprintf_safe(fp, "offline = %s\n", (int)g->offline ? "true" : "false");

	for (size_t i = 0; i < g->count; i++) {
		dep_node_t *n = &g->nodes[i];
		fprintf_safe(fp, "\n[[nodes]]\n");
		fprintf_safe(fp, "name = \"%s\"\n", n->name != nullptr ? n->name : "");

		if (n->path != nullptr) {
			sds esc = toml_escape(n->path);
			fprintf_safe(fp, "path = \"%s\"\n", esc);
			sdsfree(esc);
		}
		if (n->version != nullptr) {
			sds esc = toml_escape(n->version);
			fprintf_safe(fp, "version = \"%s\"\n", esc);
			sdsfree(esc);
		}
		if (n->version_constraint != nullptr) {
			sds esc = toml_escape(n->version_constraint);
			fprintf_safe(fp, "version_constraint = \"%s\"\n", esc);
			sdsfree(esc);
		}
		if (n->commit != nullptr) {
			sds esc = toml_escape(n->commit);
			fprintf_safe(fp, "commit = \"%s\"\n", esc);
			sdsfree(esc);
		}
		fprintf_safe(fp, "is_git = %s\n", (int)n->is_git ? "true" : "false");
		if (n->git_ref != nullptr) {
			sds esc = toml_escape(n->git_ref);
			fprintf_safe(fp, "git_ref = \"%s\"\n", esc);
			sdsfree(esc);
		}
		if (n->flags != nullptr) {
			sds esc = toml_escape(n->flags);
			fprintf_safe(fp, "flags = \"%s\"\n", esc);
			sdsfree(esc);
		}
		if (n->src_count > 0 && n->sources != nullptr) {
			fprintf_safe(fp, "sources = [");
			for (size_t j = 0; j < n->src_count; j++) {
				sds esc = toml_escape(n->sources[j]);
				fprintf_safe(fp, "\"%s\"%s", esc, (j + 1 < n->src_count) ? ", " : "");
				sdsfree(esc);
			}
			fprintf_safe(fp, "]\n");
		}
	}

	fclose(fp);
	return 0;
}

static dep_graph_t *dep_graph_load_cached(const char *cache_path, i64 toml_mtime, i64 lock_mtime)
{
	FILE *fp = fopen(cache_path, "r");
	if (fp == nullptr) {
		return nullptr;
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		return nullptr;
	}

	/* Validate metadata */
	toml_table_t *meta = toml_table_in(conf, "metadata");
	if (meta == nullptr) {
		toml_free(conf);
		return nullptr;
	}

	toml_datum_t cached_ver = toml_int_in(meta, "cache_version");
	if (!cached_ver.ok || cached_ver.u.i != 1) {
		toml_free(conf);
		return nullptr;
	}

	toml_datum_t cached_toml_mtime = toml_int_in(meta, "coffee_toml_mtime");
	if (!cached_toml_mtime.ok || cached_toml_mtime.u.i != toml_mtime) {
		toml_free(conf);
		return nullptr;
	}

	toml_datum_t cached_lock_mtime = toml_int_in(meta, "coffee_lock_mtime");
	if (!cached_lock_mtime.ok || cached_lock_mtime.u.i != lock_mtime) {
		toml_free(conf);
		return nullptr;
	}

	/* Read nodes */
	toml_array_t *nodes_arr = toml_array_in(conf, "nodes");
	if (nodes_arr == nullptr) {
		toml_free(conf);
		return nullptr;
	}

	i64 ncount = toml_array_nelem(nodes_arr);
	if (ncount <= 0) {
		toml_free(conf);
		return nullptr;
	}

	dep_graph_t *g = calloc(1, sizeof(dep_graph_t));
	if (g == nullptr) {
		toml_free(conf);
		return nullptr;
	}

	g->capacity = (size_t)ncount;
	g->nodes    = calloc(g->capacity, sizeof(dep_node_t));
	if (g->nodes == nullptr) {
		free(g);
		toml_free(conf);
		return nullptr;
	}

	/* Read offline flag from metadata */
	toml_datum_t cached_offline = toml_bool_in(meta, "offline");
	if (cached_offline.ok != 0) {
		g->offline = (cached_offline.u.b != 0);
	} else {
		g->offline = false;
	}

	for (i64 i = 0; i < ncount; i++) {
		toml_table_t *ntbl = toml_table_at(nodes_arr, i);
		if (ntbl == nullptr) {
			continue;
		}

		toml_datum_t name_d = toml_string_in(ntbl, "name");
		if (name_d.ok) {
			g->nodes[i].name = sdsnew(name_d.u.s);
			free(name_d.u.s);
		}

		toml_datum_t path_d = toml_string_in(ntbl, "path");
		if (path_d.ok) {
			g->nodes[i].path = sdsnew(path_d.u.s);
			free(path_d.u.s);
		}

		toml_datum_t ver_d = toml_string_in(ntbl, "version");
		if (ver_d.ok) {
			g->nodes[i].version = sdsnew(ver_d.u.s);
			free(ver_d.u.s);
		}

		toml_datum_t vc_d = toml_string_in(ntbl, "version_constraint");
		if (vc_d.ok) {
			g->nodes[i].version_constraint = sdsnew(vc_d.u.s);
			free(vc_d.u.s);
		}

		toml_datum_t commit_d = toml_string_in(ntbl, "commit");
		if (commit_d.ok) {
			g->nodes[i].commit = sdsnew(commit_d.u.s);
			free(commit_d.u.s);
		}

		toml_datum_t git_d = toml_bool_in(ntbl, "is_git");
		if (git_d.ok != 0) {
			g->nodes[i].is_git = (git_d.u.b != 0);
		} else {
			g->nodes[i].is_git = false;
		}

		toml_datum_t ref_d = toml_string_in(ntbl, "git_ref");
		if (ref_d.ok) {
			g->nodes[i].git_ref = sdsnew(ref_d.u.s);
			free(ref_d.u.s);
		}

		toml_datum_t flags_d = toml_string_in(ntbl, "flags");
		if (flags_d.ok) {
			g->nodes[i].flags = sdsnew(flags_d.u.s);
			free(flags_d.u.s);
		}

		toml_array_t *src_arr = toml_array_in(ntbl, "sources");
		if (src_arr != nullptr) {
			i64 src_count = toml_array_nelem(src_arr);
			if (src_count > 0) {
				g->nodes[i].sources   = calloc((size_t)src_count, sizeof(sds));
				g->nodes[i].src_count = 0;
				if (g->nodes[i].sources != nullptr) {
					for (i64 j = 0; j < src_count; j++) {
						toml_datum_t s = toml_string_at(src_arr, j);
						if (s.ok) {
							g->nodes[i].sources[g->nodes[i].src_count] = sdsnew(s.u.s);
							g->nodes[i].src_count++;
							free(s.u.s);
						}
					}
				}
			}
		}
	}

	g->count = (size_t)ncount;
	toml_free(conf);
	return g;
}

dep_graph_t *dep_graph_get(manifest_t *m, lockfile_t *lf, bool offline, const char *project_dir)
{
	if (m == nullptr) {
		return nullptr;
	}
	if (project_dir == nullptr) {
		return dep_graph_create(m, lf, offline);
	}

	/* Stat Coffee.toml and Coffee.lock for cache validation */
	i64 toml_mtime = 0;
	i64 lock_mtime = 0;

	struct stat st;
	sds         toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", project_dir);
	if (stat(toml_path, &st) == 0) {
		toml_mtime = (i64)st.st_mtime;
	}
	sdsfree(toml_path);

	sds lock_path = sdscatprintf(sdsempty(), "%s/Coffee.lock", project_dir);
	if (stat(lock_path, &st) == 0) {
		lock_mtime = (i64)st.st_mtime;
	}
	sdsfree(lock_path);

	/* Build cache path — create directory tree if needed */
	sds coffee_dir = sdscatprintf(sdsempty(), "%s/.coffee", project_dir);
	mkdir(coffee_dir, 0755);

	sds cache_dir = sdscatprintf(sdsempty(), "%s/build-cache", coffee_dir);
	mkdir(cache_dir, 0755);

	sdsfree(coffee_dir);

	const char *pkg_name   = m->package.name != nullptr ? m->package.name : "unnamed";
	sds         cache_path = sdscatprintf(sdsempty(), "%s/%s.graph", cache_dir, pkg_name);

	/* Try cache */
	dep_graph_t *g = dep_graph_load_cached(cache_path, toml_mtime, lock_mtime);
	if (g != nullptr) {
		sdsfree(cache_path);
		sdsfree(cache_dir);
		return g;
	}

	/* Cache miss — build from scratch */
	g = dep_graph_create(m, lf, offline);
	if (g != nullptr) {
		dep_graph_cache_write(g, cache_path, toml_mtime, lock_mtime);
	}

	sdsfree(cache_path);
	sdsfree(cache_dir);
	return g;
}
