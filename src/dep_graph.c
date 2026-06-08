#include "dep_graph.h"

#include "build.h"
#include "strings.h"

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
				dep_dir = dep_resolve_dir(cur->name);
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
					/* Skip if already in graph */
					if (node_contains_name(g->nodes, g->count, dep_key)) {
						continue;
					}
					/* Add as a new node */
					i64 child_idx = add_node(g, dep_key);
					if (child_idx < 0) {
						toml_free(dep_conf);
						sdsfree(dep_dir);
						dep_graph_free(g);
						return nullptr;
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
				if (!node_contains_name(g->nodes, g->count, dep_name)) {
					i64 child_idx = add_node(g, dep_name);
					if (child_idx < 0) {
						sdsfree(dep_name);
						dep_graph_free(g);
						return nullptr;
					}
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
			printf("    Fetching %s (%s)...\n", dep_name, ref);
		}
		cmd = sdscatprintf(sdsempty(),
		                   "cd '%s' && git fetch origin --depth 1 '%s' 2>/dev/null && git checkout '%s' 2>/dev/null",
		                   dep_path, ref, ref);
	} else {
		if (verbose) {
			printf("    Fetching %s...\n", dep_name);
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
