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
 * Find a structured dependency by name, or nullptr.
 */
static const dependency_t *find_manifest_dep(const manifest_t *m, const char *name)
{
	for (size_t i = 0; i < m->dependencies.deps_count; i++) {
		if (m->dependencies.deps[i].name != nullptr && strcmp(m->dependencies.deps[i].name, name) == 0) {
			return &m->dependencies.deps[i];
		}
	}
	return nullptr;
}

/*
 * True when child_path lies inside parent_dir (both realpath()-resolved).
 * A transitive path dependency may only point inside its own parent's
 * directory tree; anything else would let a third-party manifest symlink
 * an arbitrary location into deps/.  An exact match counts as inside.
 */
static bool path_within(const char *parent_dir, const char *child_path)
{
	size_t plen = strlen(parent_dir);
	if (strncmp(parent_dir, child_path, plen) != 0) {
		return false;
	}
	return child_path[plen] == '\0' || child_path[plen] == '/';
}

/*
 * Copy a structured dependency's git/path source into a graph node.
 *
 * parent_dir is nullptr for a root (user-authored, trusted) dependency, and
 * the materialized directory of the declaring dependency otherwise.  A
 * transitive path dependency is resolved against its parent and rejected
 * unless it stays inside that parent's directory; a root absolute path is
 * kept verbatim.
 */
static void apply_dep_source(dep_node_t *node, const dependency_t *sd, const char *dep_name, const char *parent_dir)
{
	if (sd->git != nullptr) {
		node->git_url = sdsnew(sd->git);
		if (sd->tag != nullptr) {
			node->git_ref = sdsnew(sd->tag);
		} else if (sd->branch != nullptr) {
			node->git_ref = sdsnew(sd->branch);
		} else if (sd->rev != nullptr) {
			node->git_ref = sdsnew(sd->rev);
		}
		return;
	}
	if (sd->path == nullptr) {
		return;
	}
	if (parent_dir == nullptr && sd->path[0] == '/') {
		node->source_path = sdsnew(sd->path);
		return;
	}

	const char *base = parent_dir != nullptr ? parent_dir : ".";
	sds         full = sdscatprintf(sdsempty(), "%s/%s", base, sd->path);
	char       *real = realpath(full, nullptr);
	sdsfree(full);

	if (real == nullptr) {
		fprintf_safe(stderr, "Warning: path '%s' for '%s' does not exist\n", sd->path, dep_name);
		return;
	}
	if (parent_dir != nullptr) {
		char *base_real = realpath(parent_dir, nullptr);
		bool  inside    = base_real != nullptr && path_within(base_real, real);
		safe_free(base_real);
		if (!inside) {
			fprintf_safe(stderr, "Warning: transitive path '%s' for '%s' escapes its parent -- ignored\n", sd->path,
			             dep_name);
			safe_free(real);
			return;
		}
	}
	node->source_path = sdsnew(real);
	safe_free(real);
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
 * Register a dependency declared in `requester`'s manifest.  Validates or
 * adds the node, records its version constraint, and copies its git/path
 * source (sd may be nullptr for a flat version string).  parent_dir is
 * nullptr for the root manifest and the declaring dep's directory for a
 * transitive dependency.  Returns 0 on success, -1 on allocation failure.
 */
static i64 register_dep(dep_graph_t *g, const char *requester, const char *name, const char *constraint,
                        const dependency_t *sd, const char *parent_dir)
{
	i64 existing = dep_graph_find_node(g, name);
	if (existing >= 0) {
		if (constraint != nullptr && g->nodes[existing].version != nullptr &&
		    !version_satisfies(g->nodes[existing].version, constraint)) {
			fprintf_safe(stderr, "Warning: %s requires %s %s but %s is resolved\n", requester, name, constraint,
			             g->nodes[existing].version);
		}
		return 0;
	}
	if (!dep_name_is_valid(name)) {
		fprintf_safe(stderr, "Warning: skipping invalid dependency name '%s'\n", name);
		return 0;
	}
	i64 child_idx = add_node(g, name);
	if (child_idx < 0) {
		return -1;
	}
	if (constraint != nullptr) {
		g->nodes[child_idx].version_constraint = sdsnew(constraint);
	}
	if (sd != nullptr) {
		apply_dep_source(&g->nodes[child_idx], sd, name, parent_dir);
	}
	return 0;
}

/*
 * Read the version from a dep directory's manifest.  Prefers
 * library.toml, falling back to Coffee.toml, so Coffee.toml-only deps
 * report a real version instead of "*".
 */
static sds read_version(const char *dep_dir)
{
	sds   toml_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
	FILE *fp        = safe_fopen(toml_path, "r");
	if (fp == nullptr) {
		sdsfree(toml_path);
		toml_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", dep_dir);
		fp        = safe_fopen(toml_path, "r");
		if (fp == nullptr) {
			sdsfree(toml_path);
			return sdsnew("*");
		}
	}
	sdsfree(toml_path);

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	safe_fclose(fp);

	if (conf == nullptr) {
		return sdsnew("*");
	}

	/* The version lives in the [package] table; keep a top-level lookup
	 * as a fallback for manifests that store it at the root. */
	toml_table_t *pkg = toml_table_in(conf, "package");
	toml_datum_t  ver = pkg != nullptr ? toml_string_in(pkg, "version") : toml_string_in(conf, "version");
	sds           result;
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
			}
			sdsfree(git_dir);

			/* Build compiler flags */
			sds flags = sdsempty();
			dep_add_flags(dep_dir, cur->name, &flags, nullptr, nullptr);
			cur->flags = flags;

			/* Collect source files */
			collect_sources(dep_dir, &cur->sources, &cur->src_count);
		}

		/* Read transitive dependencies from the dep's manifest, using the
		 * full manifest parser so inline tables and [[dependencies]] are
		 * both understood. */
		if (dep_dir != nullptr) {
			sds manifest_path = sdscatprintf(sdsempty(), "%s/Coffee.toml", dep_dir);
			if (safe_access(manifest_path, F_OK) != 0) {
				sdsfree(manifest_path);
				manifest_path = sdscatprintf(sdsempty(), "%s/library.toml", dep_dir);
			}
			manifest_t *dm = manifest_parse(manifest_path);
			sdsfree(manifest_path);

			if (dm != nullptr) {
				const char *requester = cur_name != nullptr ? cur_name : "(root)";
				/* Flat strings: "name = \"1.0\"" or a bare "name". */
				for (size_t i = 0; i < dm->package.dependencies_count; i++) {
					sds dep_name = dep_parse_name(dm->package.dependencies[i]);
					if (dep_name == nullptr) {
						continue;
					}
					if (cur_name != nullptr && strcmp(dep_name, cur_name) == 0) {
						sdsfree(dep_name);
						continue;
					}
					const dependency_t *sd = find_manifest_dep(dm, dep_name);
					sds constraint = sd != nullptr && sd->version != nullptr
					                     ? sdsnew(sd->version)
					                     : dep_extract_constraint(dm->package.dependencies[i]);
					i64 bad = register_dep(g, requester, dep_name, constraint, sd, dep_dir);
					sdsfree(constraint);
					sdsfree(dep_name);
					if (bad != 0) {
						manifest_free(dm);
						sdsfree(dep_dir);
						dep_graph_free(g);
						return nullptr;
					}
				}
				/* Structured entries (inline tables and [[dependencies]]). */
				for (size_t j = 0; j < dm->dependencies.deps_count; j++) {
					dependency_t *sd = &dm->dependencies.deps[j];
					if (sd->name == nullptr) {
						continue;
					}
					if (cur_name != nullptr && strcmp(sd->name, cur_name) == 0) {
						continue;
					}
					if (register_dep(g, requester, sd->name, sd->version, sd, dep_dir) != 0) {
						manifest_free(dm);
						sdsfree(dep_dir);
						dep_graph_free(g);
						return nullptr;
					}
				}
				manifest_free(dm);
			}
		}

		/* For root (dep_cursor == 1 after increment), also process root's own deps */
		if (dep_cursor == 1) {
			const char *requester = m->package.name != nullptr ? m->package.name : "(root)";

			/* Flat dependencies.  An inline-table dep also has a bare-key
			 * remnant here, which is how its git/path source reaches the
			 * node (apply_dep_source below). */
			for (size_t i = 0; i < m->package.dependencies_count; i++) {
				sds dep_name = dep_parse_name(m->package.dependencies[i]);
				if (dep_name == nullptr) {
					continue;
				}
				const dependency_t *sd = find_manifest_dep(m, dep_name);
				sds constraint = sd != nullptr && sd->version != nullptr
				                     ? sdsnew(sd->version)
				                     : dep_extract_constraint(m->package.dependencies[i]);
				i64 bad = register_dep(g, requester, dep_name, constraint, sd, nullptr);
				sdsfree(constraint);
				sdsfree(dep_name);
				if (bad != 0) {
					dep_graph_free(g);
					return nullptr;
				}
			}

			/* Structured entries with no flat remnant ([[dependencies]]). */
			for (size_t j = 0; j < m->dependencies.deps_count; j++) {
				dependency_t *sd = &m->dependencies.deps[j];
				if (sd->name == nullptr) {
					continue;
				}
				if (register_dep(g, requester, sd->name, sd->version, sd, nullptr) != 0) {
					dep_graph_free(g);
					return nullptr;
				}
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
		sdsfree(g->nodes[i].git_url);
		sdsfree(g->nodes[i].source_path);
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

