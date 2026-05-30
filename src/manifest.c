#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strings.h"

static char *strdup_or_null(const char *s)
{
	if (s == NULL) {
		return NULL;
	}
	return strdup(s);
}

static bool is_valid_feature_name(const char *name)
{
	if (!name || name[0] == '\0') {
		return false;
	}
	for (size_t i = 0; name[i] != '\0'; i++) {
		char c = name[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
			return false;
		}
	}
	return true;
}

static void free_package(package_t *pkg)
{
	if (pkg->name) {
		free(pkg->name);
	}
	if (pkg->version) {
		free(pkg->version);
	}
	if (pkg->edition) {
		free(pkg->edition);
	}
	if (pkg->description) {
		free(pkg->description);
	}
	if (pkg->license) {
		free(pkg->license);
	}
	if (pkg->repository) {
		free(pkg->repository);
	}
	if (pkg->authors) {
		free(pkg->authors);
	}

	for (size_t i = 0; i < pkg->dependencies_count; i++) {
		free(pkg->dependencies[i]);
	}
	free(pkg->dependencies);

	for (size_t i = 0; i < pkg->sources_count; i++) {
		free(pkg->sources[i]);
	}
	free(pkg->sources);

	for (size_t i = 0; i < pkg->headers_count; i++) {
		free(pkg->headers[i]);
	}
	free(pkg->headers);
}

static void free_feature(feature_def_t *feat)
{
	if (feat->name) {
		free(feat->name);
	}
	for (size_t i = 0; i < feat->deps_count; i++) {
		free(feat->deps[i]);
	}
	free(feat->deps);
}

static void free_dependency(dependency_t *dep)
{
	if (dep->name) {
		free(dep->name);
	}
	if (dep->version) {
		free(dep->version);
	}
	if (dep->path) {
		free(dep->path);
	}
	if (dep->git) {
		free(dep->git);
	}
	if (dep->branch) {
		free(dep->branch);
	}
	if (dep->tag) {
		free(dep->tag);
	}
	if (dep->rev) {
		free(dep->rev);
	}
}

static char *toml_datum_to_string(toml_datum_t datum)
{
	if (!datum.ok) {
		return NULL;
	}
	return datum.u.s;
}

manifest_t *manifest_parse(sds path)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		return NULL;
	}

	char		  errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (!conf) {
		return NULL;
	}

	manifest_t *m = calloc(1, sizeof(manifest_t));
	if (!m) {
		toml_free(conf);
		return NULL;
	}

	toml_table_t *pkg = toml_table_in(conf, "package");
	if (pkg) {
		toml_datum_t name = toml_string_in(pkg, "name");
		m->package.name	  = toml_datum_to_string(name);

		toml_datum_t version = toml_string_in(pkg, "version");
		m->package.version	 = toml_datum_to_string(version);

		toml_datum_t edition = toml_string_in(pkg, "edition");
		m->package.edition	 = toml_datum_to_string(edition);

		toml_datum_t description = toml_string_in(pkg, "description");
		m->package.description	 = toml_datum_to_string(description);

		toml_datum_t license = toml_string_in(pkg, "license");
		m->package.license	 = toml_datum_to_string(license);

		toml_datum_t repository = toml_string_in(pkg, "repository");
		m->package.repository	= toml_datum_to_string(repository);

		toml_datum_t authors = toml_string_in(pkg, "authors");
		m->package.authors	 = toml_datum_to_string(authors);
	}

	toml_array_t *deps_arr = toml_array_in(conf, "dependencies");
	if (deps_arr) {
		m->package.dependencies_count = toml_array_nelem(deps_arr);
		m->package.dependencies		  = calloc(m->package.dependencies_count, sizeof(char *));
		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			toml_datum_t dep		   = toml_string_at(deps_arr, i);
			m->package.dependencies[i] = toml_datum_to_string(dep);
		}
	} else {
		/* Also try table format: [dependencies]\nname = "version" */
		toml_table_t *deps_table = toml_table_in(conf, "dependencies");
		if (deps_table) {
			/* Count entries */
			size_t count = 0;
			for (int i = 0;; i++) {
				const char *key = toml_key_in(deps_table, i);
				if (!key) {
					break;
				}
				count++;
			}
			m->package.dependencies_count = count;
			m->package.dependencies		  = calloc(count, sizeof(char *));
			size_t idx					  = 0;
			/* Build "name = value" strings matching array format */
			for (int i = 0; idx < count; i++) {
				const char *key = toml_key_in(deps_table, i);
				if (!key) {
					break;
				}
				toml_datum_t val = toml_string_in(deps_table, key);
				size_t		 len;
				char		*str;
				if (val.ok) {
					char *vstr = toml_datum_to_string(val);
					len		   = strlen(key) + strlen(" = \"") + strlen(vstr) + 2;
					str		   = malloc(len);
					snprintf_safe(str, len, "%s = \"%s\"", key, vstr);
					free(vstr);
				} else {
					str = strdup(key);
				}
				if (str) {
					m->package.dependencies[idx] = str;
					idx++;
				}
			}
		}
	}

	toml_array_t *sources_arr = toml_array_in(conf, "sources");
	if (sources_arr) {
		m->package.sources_count = toml_array_nelem(sources_arr);
		m->package.sources		 = calloc(m->package.sources_count, sizeof(char *));
		for (size_t i = 0; i < m->package.sources_count; i++) {
			toml_datum_t src	  = toml_string_at(sources_arr, i);
			m->package.sources[i] = toml_datum_to_string(src);
		}
	}

	toml_array_t *headers_arr = toml_array_in(conf, "headers");
	if (headers_arr) {
		m->package.headers_count = toml_array_nelem(headers_arr);
		m->package.headers		 = calloc(m->package.headers_count, sizeof(char *));
		for (size_t i = 0; i < m->package.headers_count; i++) {
			toml_datum_t hdr	  = toml_string_at(headers_arr, i);
			m->package.headers[i] = toml_datum_to_string(hdr);
		}
	}

	toml_table_t *features_table = toml_table_in(conf, "features");
	if (features_table) {
		m->features_count = 0;
		for (int i = 0;; i++) {
			const char *key = toml_key_in(features_table, i);
			if (!key) {
				break;
			}
			toml_array_t *arr = toml_array_in(features_table, key);
			if (!arr) {
				continue;
			}
			m->features_count++;
		}

		if (m->features_count > 0) {
			m->features = calloc(m->features_count, sizeof(feature_def_t));
			size_t idx	= 0;
			for (int i = 0;; i++) {
				const char *key = toml_key_in(features_table, i);
				if (!key) {
					break;
				}
				toml_array_t *arr = toml_array_in(features_table, key);
				if (!arr) {
					continue;
				}
				m->features[idx].name = strdup(key);
				if (!is_valid_feature_name(m->features[idx].name)) {
					fprintf_safe(stderr, "Warning: Invalid feature name: %s\n", key);
				}
				m->features[idx].deps_count = toml_array_nelem(arr);
				if (m->features[idx].deps_count > 0) {
					m->features[idx].deps = calloc(m->features[idx].deps_count, sizeof(char *));
					for (size_t j = 0; j < m->features[idx].deps_count; j++) {
						toml_datum_t dep		 = toml_string_at(arr, j);
						m->features[idx].deps[j] = toml_datum_to_string(dep);
					}
				}
				idx++;
			}
		}
	}

	for (size_t i = 0; i < m->features_count; i++) {
		for (size_t j = 0; j < m->features[i].deps_count; j++) {
			char *dep = m->features[i].deps[j];
			if (!dep) {
				continue;
			}
			for (size_t k = 0; k < m->features_count; k++) {
				if (k == i) {
					continue;
				}
				if (m->features[k].name && strcmp(dep, m->features[k].name) == 0) {
					for (size_t l = 0; l < m->features[k].deps_count; l++) {
						if (m->features[k].deps[l] && strcmp(m->features[k].deps[l], m->features[i].name) == 0) {
							fprintf_safe(stderr,
										 "Warning: Circular feature dependency detected: %s <-> "
										 "%s\n",
										 m->features[i].name, m->features[k].name);
						}
					}
				}
			}
		}
	}

	toml_free(conf);
	return m;
}

void manifest_extract_dep_info(const char *entry, char **name_out, char **version_out)
{
	if (!entry || !name_out || !version_out) {
		if (name_out) {
			*name_out = NULL;
		}
		if (version_out) {
			*version_out = NULL;
		}
		return;
	}

	*name_out	 = NULL;
	*version_out = NULL;

	const char *eq = strchr(entry, '=');
	if (!eq) {
		const char *end = entry + strlen(entry);
		while (end > entry && (*(end - 1) == ' ' || *(end - 1) == '\t')) {
			end--;
		}
		size_t len = (size_t)(end - entry);
		*name_out  = malloc(len + 1);
		if (*name_out) {
			memccpy(*name_out, entry, '\0', len);
			(*name_out)[len] = '\0';
		}
		*version_out = strdup("*");
		return;
	}

	const char *name_end = eq - 1;
	while (name_end > entry && (*name_end == ' ' || *name_end == '\t')) {
		name_end--;
	}
	size_t name_len = (size_t)(name_end - entry + 1);
	*name_out		= malloc(name_len + 1);
	if (*name_out) {
		memccpy(*name_out, entry, '\0', name_len);
		(*name_out)[name_len] = '\0';
	}

	const char *ver_start = eq + 1;
	while (*ver_start == ' ' || *ver_start == '\t') {
		ver_start++;
	}

	if (*ver_start == '"') {
		ver_start++;
	}
	const char *ver_end = ver_start + strlen(ver_start);
	while (ver_end > ver_start && (*(ver_end - 1) == ' ' || *(ver_end - 1) == '\t' || *(ver_end - 1) == '"')) {
		ver_end--;
	}

	size_t ver_len = (size_t)(ver_end - ver_start);
	*version_out   = malloc(ver_len + 1);
	if (*version_out) {
		memccpy(*version_out, ver_start, '\0', ver_len);
		(*version_out)[ver_len] = '\0';
	}
}

void manifest_free(manifest_t *m)
{
	if (!m) {
		return;
	}
	free_package(&m->package);

	for (size_t i = 0; i < m->dependencies.deps_count; i++) {
		free_dependency(&m->dependencies.deps[i]);
	}
	free(m->dependencies.deps);

	for (size_t i = 0; i < m->features_count; i++) {
		free_feature(&m->features[i]);
	}
	free(m->features);

	free(m);
}

int manifest_write(sds path, manifest_t *m)
{
	FILE *fp = fopen(path, "w");
	if (!fp) {
		return -1;
	}

	fprintf_safe(fp, "[package]\n");
	if (m->package.name) {
		fprintf_safe(fp, "name = \"%s\"\n", m->package.name);
	}
	if (m->package.version) {
		fprintf_safe(fp, "version = \"%s\"\n", m->package.version);
	}
	if (m->package.edition) {
		fprintf_safe(fp, "edition = \"%s\"\n", m->package.edition);
	}
	if (m->package.description) {
		fprintf_safe(fp, "description = \"%s\"\n", m->package.description);
	}
	if (m->package.license) {
		fprintf_safe(fp, "license = \"%s\"\n", m->package.license);
	}
	if (m->package.repository) {
		fprintf_safe(fp, "repository = \"%s\"\n", m->package.repository);
	}
	if (m->package.authors) {
		fprintf_safe(fp, "authors = \"%s\"\n", m->package.authors);
	}

	if (m->package.dependencies_count > 0) {
		fprintf_safe(fp, "\n[dependencies]\n");
		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			if (m->package.dependencies[i]) {
				fprintf_safe(fp, "%s\n", m->package.dependencies[i]);
			}
		}
	}

	if (m->package.sources_count > 0) {
		fprintf_safe(fp, "\nsources = [\n");
		for (size_t i = 0; i < m->package.sources_count; i++) {
			if (m->package.sources[i]) {
				fprintf_safe(fp, "  \"%s\",\n", m->package.sources[i]);
			}
		}
		fprintf_safe(fp, "]\n");
	}

	if (m->package.headers_count > 0) {
		fprintf_safe(fp, "\nheaders = [\n");
		for (size_t i = 0; i < m->package.headers_count; i++) {
			if (m->package.headers[i]) {
				fprintf_safe(fp, "  \"%s\",\n", m->package.headers[i]);
			}
		}
		fprintf_safe(fp, "]\n");
	}

	if (m->features_count > 0) {
		fprintf_safe(fp, "\n[features]\n");
		for (size_t i = 0; i < m->features_count; i++) {
			if (m->features[i].name) {
				fprintf_safe(fp, "%s = [", m->features[i].name);
				for (size_t j = 0; j < m->features[i].deps_count; j++) {
					if (j > 0) {
						fprintf_safe(fp, ", ");
					}
					if (m->features[i].deps[j]) {
						fprintf_safe(fp, "\"%s\"", m->features[i].deps[j]);
					}
				}
				fprintf_safe(fp, "]\n");
			}
		}
	}

	fclose(fp);
	return 0;
}
