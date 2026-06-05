#include "registry.h"

#include "strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ctype.h>
#include <sds/sds.h>
#include <sys/stat.h>
#include <toml.h>
#include <unistd.h>

sds coffee_home_dir(void)
{
	static char home_dir[4096];
	const char *coffee_home = getenv("COFFEE_HOME");
	if (coffee_home) {
		snprintf_safe(home_dir, sizeof(home_dir), "%s", coffee_home);
	} else {
		const char *home = getenv("HOME");
		if (home == nullptr) {
			home = "/tmp";
		}
		snprintf_safe(home_dir, sizeof(home_dir), "%s/.coffee", home);
	}
	return home_dir;
}

static const char *get_cache_dir(void)
{
	return coffee_home_dir();
}

static const char *get_index_path(void)
{
	static char index_path[4096];
	snprintf_safe(index_path, sizeof(index_path), "%s/packages.json", get_cache_dir());
	return index_path;
}

static i64 ensure_index_cached(void)
{
	const char *index_path = get_index_path();
	struct stat st;

	if (stat(index_path, &st) == 0) {
		time_t now = time(nullptr);
		if (now - st.st_mtime < 300) {
			return 0;
		}
	}

	const char *cache_dir = get_cache_dir();
	char        cmd[4096];
	snprintf_safe(cmd, sizeof(cmd), "mkdir -p %s", cache_dir);
	(void)system(cmd);

	snprintf_safe(cmd, sizeof(cmd), "curl -sL \"" REGISTRY_INDEX_URL "\" | zstd -df -o %s 2>/dev/null", index_path);

	return system(cmd);
}

static char *fetch_url(const char *url)
{
	char cmd[4096];
	snprintf_safe(cmd, sizeof(cmd), "curl -sL \"%s\" 2>/dev/null", url);

	FILE *fp = popen(cmd, "r");
	if (fp == nullptr) {
		return nullptr;
	}

	char *buffer = malloc(1);
	buffer[0]    = '\0';
	size_t total = 0;
	char   buf[4096];

	while (fgets(buf, sizeof(buf), fp)) {
		size_t len    = strlen(buf);
		char  *newbuf = realloc(buffer, total + len + 1);
		if (newbuf == nullptr) {
			free(buffer);
			pclose(fp);
			return nullptr;
		}
		buffer = newbuf;
		memccpy(buffer + total, buf, '\0', len);
		total += len;
		buffer[total] = '\0';
	}

	pclose(fp);
	return buffer;
}

static char *extract_string_val(const char *text, const char *key)
{
	const char *p      = text;
	size_t      keylen = strlen(key);

	while (*p != '\0') {
		while (*p == ' ' || *p == '\t' || *p == '\n') {
			p++;
		}

		const char *after_key = nullptr;

		if (*p == '\"') {
			if (strncmp(p + 1, key, keylen) == 0 && p[1 + keylen] == '\"') {
				after_key = p + 1 + keylen + 1;
			}
		} else if (strncmp(p, key, keylen) == 0) {
			after_key = p + keylen;
		}

		if (after_key) {
			while (*after_key == ' ' || *after_key == '\t') {
				after_key++;
			}
			if (*after_key == ':' || *after_key == '=') {
				after_key++;
				while (*after_key == ' ' || *after_key == '\t') {
					after_key++;
				}
				if (*after_key == '\"') {
					after_key++;
					const char *start = after_key;
					while (*after_key != '\0' && *after_key != '\"' && *after_key != '\n') {
						after_key++;
					}
					if (*after_key == '\"' && after_key > start) {
						char *result = malloc((size_t)(after_key - start + 1));
						memccpy(result, start, '\0', (size_t)(after_key - start));
						result[after_key - start] = '\0';
						return result;
					}
				}
			}
		}

		while (*p != '\0' && *p != '\n') {
			p++;
		}
		if (*p == '\n') {
			p++;
		}
	}

	return nullptr;
}

static recipe_list_t *parse_package_list(const char *json)
{
	recipe_list_t *list = calloc(1, sizeof(recipe_list_t));
	if (list == nullptr) {
		return nullptr;
	}

	const char *p           = json;
	i64         brace_count = 0;
	const char *obj_start   = nullptr;

	while (*p != '\0') {
		if (*p == '{') {
			const char *q = p + 1;
			while (*q == ' ' || *q == '\n' || *q == '\t') {
				q++;
			}
			if (strncmp(q, "\"name\"", 6) == 0) {
				obj_start   = p;
				brace_count = 0;
			}
		}

		if (obj_start) {
			if (*p == '{') {
				brace_count++;
			}
			if (*p == '}') {
				brace_count--;
			}

			if (brace_count == 0 && obj_start) {
				size_t obj_len = (size_t)(p - obj_start + 1);
				char  *obj     = malloc(obj_len + 1);
				memccpy(obj, obj_start, '\0', obj_len);
				obj[obj_len] = '\0';

				recipe_t new_r;
				memset(&new_r, 0, sizeof(recipe_t));
				new_r.name        = extract_string_val(obj, "name");
				new_r.version     = extract_string_val(obj, "version");
				new_r.description = extract_string_val(obj, "description");

				recipe_t *new_recipes = realloc(list->recipes, (list->count + 1) * sizeof(recipe_t));
				if (new_recipes) {
					list->recipes                = new_recipes;
					list->recipes[list->count++] = new_r;
				}
				free(obj);
				obj_start = nullptr;
			}
		}
		p++;
	}

	return list;
}

recipe_list_t *registry_search(sds query)
{
	if (ensure_index_cached() != 0) {
		recipe_list_t *empty = calloc(1, sizeof(recipe_list_t));
		return empty;
	}

	const char *index_path = get_index_path();
	FILE       *fp         = fopen(index_path, "r");
	if (fp == nullptr) {
		recipe_list_t *empty = calloc(1, sizeof(recipe_list_t));
		return empty;
	}

	fseek(fp, 0, SEEK_END);
	long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *json = malloc((size_t)(len + 1));
	fread(json, 1, (size_t)len, fp);
	json[len] = '\0';
	fclose(fp);

	recipe_list_t *all = parse_package_list(json);
	free(json);

	if (query == nullptr || strlen(query) == 0) {
		return all;
	}

	recipe_list_t *filtered = calloc(1, sizeof(recipe_list_t));
	if (filtered == nullptr) {
		return all;
	}

	for (size_t i = 0; i < all->count; i++) {
		recipe_t *r     = &all->recipes[i];
		i64       match = 0;

		if (r->name != nullptr && strcasestr(r->name, query)) {
			match = 1;
		}
		if (r->description && strcasestr(r->description, query)) {
			match = 1;
		}

		if (match) {
			recipe_t new_r;
			memset(&new_r, 0, sizeof(recipe_t));
			if (r->name) {
				size_t nlen = strlen(r->name);
				new_r.name  = malloc(nlen + 1);
				if (new_r.name) {
					memccpy(new_r.name, r->name, '\0', nlen);
					new_r.name[nlen] = '\0';
				}
			}
			if (r->version) {
				size_t vlen   = strlen(r->version);
				new_r.version = malloc(vlen + 1);
				if (new_r.version) {
					memccpy(new_r.version, r->version, '\0', vlen);
					new_r.version[vlen] = '\0';
				}
			}
			if (r->description) {
				size_t dlen       = strlen(r->description);
				new_r.description = malloc(dlen + 1);
				if (new_r.description) {
					memccpy(new_r.description, r->description, '\0', dlen);
					new_r.description[dlen] = '\0';
				}
			}

			recipe_t *new_recipes = realloc(filtered->recipes, (filtered->count + 1) * sizeof(recipe_t));
			if (new_recipes) {
				filtered->recipes                    = new_recipes;
				filtered->recipes[filtered->count++] = new_r;
			}
		}
	}

	return filtered;
}

recipe_t *registry_get(sds name)
{
	if (name == nullptr || strlen(name) == 0) {
		return nullptr;
	}

	char first = (char)tolower((unsigned char)name[0]);
	char url[4096];
	snprintf_safe(url, sizeof(url), REGISTRY_RAW_URL "/recipes/%c/%s/library.toml", first, name);

	char *meta = fetch_url(url);
	if (meta == nullptr) {
		return nullptr;
	}

	recipe_t *r = calloc(1, sizeof(recipe_t));
	if (r == nullptr) {
		free(meta);
		return nullptr;
	}

	size_t nlen = strlen(name);
	r->name     = malloc(nlen + 1);
	if (r->name) {
		memccpy(r->name, name, '\0', nlen);
		r->name[nlen] = '\0';
	}

	/* Parse the fetched library.toml with vendored tomlc99 */
	char          errbuf[256];
	toml_table_t *tbl = toml_parse(meta, errbuf, sizeof(errbuf));
	if (tbl) {
		toml_datum_t v;
		v = toml_string_in(tbl, "version");
		if (v.ok) {
			r->version = v.u.s; /* steal pointer — freed via registry_free_recipe */
		}
		v = toml_string_in(tbl, "license");
		if (v.ok) {
			r->license = v.u.s;
		}
		v = toml_string_in(tbl, "description");
		if (v.ok) {
			r->description = v.u.s;
		}
		v = toml_string_in(tbl, "recipe_url");
		if (v.ok) {
			r->download_url = v.u.s;
		}
		v = toml_string_in(tbl, "dependencies");
		if (v.ok) {
			r->dependencies = v.u.s;
		}

		toml_free(tbl);
	}

	free(meta);
	return r;
}

i64 registry_fetch(sds name, const char *version, sds dest_dir)
{
	(void)version;
	if (name == nullptr || !dest_dir) {
		return -1;
	}

	char first = (char)tolower((unsigned char)name[0]);
	char cmd[4096];

	snprintf_safe(cmd, sizeof(cmd), "mkdir -p %s", dest_dir);
	if (system(cmd) != 0) {
		return -1;
	}

	snprintf_safe(cmd, sizeof(cmd), "curl -sL \"" REGISTRY_RAW_URL "/recipes/%c/%s/library.toml\" -o %s/library.toml",
	              first, name, dest_dir);
	if (system(cmd) != 0) {
		return -1;
	}

	snprintf_safe(cmd, sizeof(cmd),
	              "curl -sL \"" REGISTRY_RAW_URL "/recipes/%c/%s/install.sh\" -o %s/install.sh 2>/dev/null", first,
	              name, dest_dir);
	system(cmd);

	return 0;
}

version_list_t *registry_get_versions(sds name)
{
	if (name == nullptr || strlen(name) == 0) {
		return nullptr;
	}

	char first = (char)tolower((unsigned char)name[0]);
	char url[4096];
	snprintf_safe(url, sizeof(url), REGISTRY_RAW_URL "/recipes/%c/%s/library.toml", first, name);

	char *meta = fetch_url(url);
	if (meta == nullptr) {
		return nullptr;
	}

	version_list_t *list = calloc(1, sizeof(version_list_t));
	if (list == nullptr) {
		free(meta);
		return nullptr;
	}

	/* Parse with vendored tomlc99 */
	char          errbuf[256];
	toml_table_t *tbl = toml_parse(meta, errbuf, sizeof(errbuf));
	if (tbl) {
		toml_datum_t v = toml_string_in(tbl, "version");
		if (v.ok) {
			list->versions = malloc(sizeof(char *));
			if (list->versions) {
				list->versions[0] = v.u.s; /* steal pointer */
				list->count       = 1;
			} else {
				free(v.u.s);
			}
		}
		toml_free(tbl);
	}

	free(meta);
	return list;
}

void registry_free_versions(version_list_t *list)
{
	if (list == nullptr) {
		return;
	}

	for (size_t i = 0; i < list->count; i++) {
		if (list->versions[i]) {
			free(list->versions[i]);
		}
	}
	free(list->versions);
	free(list);
}

void registry_free_recipes(recipe_list_t *list)
{
	if (list == nullptr) {
		return;
	}

	for (size_t i = 0; i < list->count; i++) {
		recipe_t *r = &list->recipes[i];
		if (r->name) {
			free(r->name);
		}
		if (r->version) {
			free(r->version);
		}
		if (r->license) {
			free(r->license);
		}
		if (r->repo) {
			free(r->repo);
		}
		if (r->description) {
			free(r->description);
		}
		if (r->download_url) {
			free(r->download_url);
		}
		if (r->dependencies) {
			free(r->dependencies);
		}
	}
	free(list->recipes);
	free(list);
}

void registry_free_recipe(recipe_t *r)
{
	if (r == nullptr) {
		return;
	}
	if (r->name) {
		free(r->name);
	}
	if (r->version) {
		free(r->version);
	}
	if (r->license) {
		free(r->license);
	}
	if (r->repo) {
		free(r->repo);
	}
	if (r->description) {
		free(r->description);
	}
	if (r->download_url) {
		free(r->download_url);
	}
	if (r->dependencies) {
		free(r->dependencies);
	}
	free(r);
}
