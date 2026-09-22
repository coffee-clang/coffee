#include "dep_graph.h"

#include "build.h"
#include "safe.h"
#include "strings.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glob.h>
#include <sds/sds.h>
#include <toml.h>

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
		safe_free(str_val.u.s);
		return result;
	}

	toml_table_t *inline_tbl = toml_table_in(table, key);
	if (inline_tbl != nullptr) {
		toml_datum_t ver = toml_string_in(inline_tbl, "version");
		if (ver.ok) {
			sds result = sdsnew(ver.u.s);
			safe_free(ver.u.s);
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

/*
 * Find a dep in the graph by name. Returns -1 if not found.
 */
i64 dep_graph_find_node(const dep_graph_t *g, const char *name)
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
		g->nodes       = safe_realloc(g->nodes, new_cap * sizeof(dep_node_t));
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
	FILE *fp        = safe_fopen(toml_path, "r");
	if (fp == nullptr) {
		sdsfree(toml_path);
		toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
		fp        = safe_fopen(toml_path, "r");
		if (fp == nullptr) {
			sdsfree(toml_path);
			return nullptr;
		}
	}
	sdsfree(toml_path);

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);

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
	FILE *fp        = safe_fopen(toml_path, "r");
	sdsfree(toml_path);
	if (fp == nullptr) {
		return sdsnew("*");
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);

	if (conf == nullptr) {
		return sdsnew("*");
	}

	toml_datum_t ver = toml_string_in(conf, "version");
	sds          result;
	if (ver.ok) {
		result = sdsnew(ver.u.s);
		safe_free(ver.u.s);
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
	FILE *fp        = safe_fopen(toml_path, "r");
	if (fp == nullptr) {
		sdsfree(toml_path);
		toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
		fp        = safe_fopen(toml_path, "r");
		if (fp == nullptr) {
			sdsfree(toml_path);
			return nullptr;
		}
	}
	sdsfree(toml_path);

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);

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
				safe_free(tag.u.s);
				break;
			}
			toml_datum_t branch = toml_string_in(tbl, "branch");
			if (branch.ok) {
				ref = sdsnew(branch.u.s);
				safe_free(branch.u.s);
				break;
			}
			toml_datum_t rev = toml_string_in(tbl, "rev");
			if (rev.ok) {
				ref = sdsnew(rev.u.s);
				safe_free(rev.u.s);
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
			*out_src = safe_calloc((size_t)gbuf.gl_pathc, sizeof(sds));
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

	dep_graph_t *g = safe_calloc(1, sizeof(dep_graph_t));
	if (g == nullptr) {
		return nullptr;
	}
	g->offline = offline;

	/* Add root node */
	const char *root_name = m->package.name != nullptr ? m->package.name : "root";
	if (!dep_name_is_valid(root_name)) {
		fprintf_safe(stderr, "Error: invalid root package name '%s' (only [A-Za-z0-9_-] allowed)\n", root_name);
		dep_graph_free(g);
		return nullptr;
	}
	i64 root_idx = add_node(g, root_name);
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
		/* add_node() below can realloc g->nodes, invalidating cur; the
		 * sds buffer cur->name points to does not move, so capture it. */
		const char *cur_name = cur->name;

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
			if (safe_access(git_dir, F_OK) == 0) {
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
					if (cur_name != nullptr && strcmp(dep_key, cur_name) == 0) {
						continue;
					}
					/* Extract version constraint for this dep */
					sds constraint = get_toml_constraint(transitive_deps, dep_key);

					/* Check if already in graph — validate version constraint */
					i64 existing = dep_graph_find_node(g, dep_key);
					if (existing >= 0) {
						if (constraint != nullptr && g->nodes[existing].version != nullptr) {
							if (!version_satisfies(g->nodes[existing].version, constraint)) {
								fprintf_safe(stderr, "Warning: %s requires %s %s but %s is resolved\n",
								             cur_name != nullptr ? cur_name : "(root)", dep_key, constraint,
								             g->nodes[existing].version);
							}
						}
						sdsfree(constraint);
						continue;
					}
					if (!dep_name_is_valid(dep_key)) {
						fprintf_safe(stderr, "Warning: skipping invalid dependency name '%s'\n", dep_key);
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
				i64 existing = dep_graph_find_node(g, dep_name);
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

				if (!dep_name_is_valid(dep_name)) {
					fprintf_safe(stderr, "Warning: skipping invalid dependency name '%s'\n", dep_name);
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
		safe_free(g->nodes[i].sources);
	}
	safe_free(g->nodes);
	safe_free(g);
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
	sds *names = safe_calloc(g->count, sizeof(sds));
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
	i64 idx = dep_graph_find_node(g, dep_name);
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
	i64 idx = dep_graph_find_node(g, dep_name);
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
	i64 idx = dep_graph_find_node(g, dep_name);
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
	i64 idx = dep_graph_find_node(g, dep_name);
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
	i64 idx = dep_graph_find_node(g, dep_name);
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
	i64 idx = dep_graph_find_node(g, dep_name);
	if (idx < 0) {
		return nullptr;
	}
	return g->nodes[idx].git_ref;
}

