#include "manifest.h"

#include "strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_valid_feature_name(const char *name)
{
	if (name == nullptr || name[0] == '\0') {
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
	sdsfree(pkg->name);
	sdsfree(pkg->version);
	sdsfree(pkg->edition);
	sdsfree(pkg->description);
	sdsfree(pkg->license);
	sdsfree(pkg->repository);
	sdsfree(pkg->authors);

	for (size_t i = 0; i < pkg->dependencies_count; i++) {
		sdsfree(pkg->dependencies[i]);
	}
	free(pkg->dependencies);

	for (size_t i = 0; i < pkg->sources_count; i++) {
		sdsfree(pkg->sources[i]);
	}
	free(pkg->sources);

	for (size_t i = 0; i < pkg->headers_count; i++) {
		sdsfree(pkg->headers[i]);
	}
	free(pkg->headers);
}

static void free_feature(feature_def_t *feat)
{
	sdsfree(feat->name);
	for (size_t i = 0; i < feat->deps_count; i++) {
		sdsfree(feat->deps[i]);
	}
	free(feat->deps);
}

static void free_dependency(dependency_t *dep)
{
	sdsfree(dep->name);
	sdsfree(dep->version);
	sdsfree(dep->path);
	sdsfree(dep->git);
	sdsfree(dep->branch);
	sdsfree(dep->tag);
	sdsfree(dep->rev);
}

static void free_binary(binary_target_t *bin)
{
	sdsfree(bin->name);
	for (size_t i = 0; i < bin->src_count; i++) {
		sdsfree(bin->src[i]);
	}
	free(bin->src);
}

static sds toml_datum_to_string(toml_datum_t datum)
{
	if (!datum.ok) {
		return nullptr;
	}
	sds result = sdsnew(datum.u.s);
	free(datum.u.s);
	return result;
}

manifest_t *manifest_parse(sds path)
{
	FILE *fp = fopen(path, "r");
	if (fp == nullptr) {
		return nullptr;
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		return nullptr;
	}

	manifest_t *m = calloc(1, sizeof(manifest_t));
	if (m == nullptr) {
		toml_free(conf);
		return nullptr;
	}

	toml_table_t *pkg = toml_table_in(conf, "package");
	if (pkg) {
		toml_datum_t name = toml_string_in(pkg, "name");
		m->package.name   = toml_datum_to_string(name);

		toml_datum_t version = toml_string_in(pkg, "version");
		m->package.version   = toml_datum_to_string(version);

		toml_datum_t edition = toml_string_in(pkg, "edition");
		m->package.edition   = toml_datum_to_string(edition);

		toml_datum_t description = toml_string_in(pkg, "description");
		m->package.description   = toml_datum_to_string(description);

		toml_datum_t license = toml_string_in(pkg, "license");
		m->package.license   = toml_datum_to_string(license);

		toml_datum_t repository = toml_string_in(pkg, "repository");
		m->package.repository   = toml_datum_to_string(repository);

		toml_datum_t authors = toml_string_in(pkg, "authors");
		m->package.authors   = toml_datum_to_string(authors);
	}

	toml_array_t *deps_arr = toml_array_in(conf, "dependencies");
	if (deps_arr) {
		m->package.dependencies_count = (size_t)toml_array_nelem(deps_arr);
		m->package.dependencies       = calloc(m->package.dependencies_count, sizeof(sds));
		if (m->package.dependencies == nullptr && m->package.dependencies_count > 0) {
			manifest_free(m);
			toml_free(conf);
			return nullptr;
		}
		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			toml_datum_t dep           = toml_string_at(deps_arr, (i64)i);
			m->package.dependencies[i] = toml_datum_to_string(dep);
		}
	} else {
		/* Also try table format: [dependencies]\nname = "version" */
		toml_table_t *deps_table = toml_table_in(conf, "dependencies");
		if (deps_table) {
			/* Count entries */
			size_t count = 0;
			for (i64 i = 0;; i++) {
				const char *key = toml_key_in(deps_table, i);
				if (key == nullptr) {
					break;
				}
				count++;
			}
			m->package.dependencies_count = count;
			m->package.dependencies       = calloc(count, sizeof(sds));
			if (m->package.dependencies == nullptr && count > 0) {
				manifest_free(m);
				toml_free(conf);
				return nullptr;
			}
			size_t idx = 0;
			/* Build "name = value" strings matching array format */
			for (i64 i = 0; idx < count; i++) {
				const char *key = toml_key_in(deps_table, i);
				if (key == nullptr) {
					break;
				}
				toml_datum_t val = toml_string_in(deps_table, key);
				sds          str;
				if (val.ok) {
					sds vstr = toml_datum_to_string(val);
					str      = sdscatfmt(sdsnew(key), " = \"%s\"", vstr);
					sdsfree(vstr);
				} else {
					str = sdsnew(key);
				}
				if (str) {
					m->package.dependencies[idx] = str;
					idx++;
				}
			}
		}
	}

	toml_array_t *sources_arr = toml_array_in(conf, "sources");
	if (sources_arr == nullptr && pkg != nullptr) {
		sources_arr = toml_array_in(pkg, "sources");
	}
	if (sources_arr) {
		m->package.sources_count = (size_t)toml_array_nelem(sources_arr);
		m->package.sources       = calloc(m->package.sources_count, sizeof(sds));
		if (m->package.sources == nullptr && m->package.sources_count > 0) {
			manifest_free(m);
			toml_free(conf);
			return nullptr;
		}
		for (size_t i = 0; i < m->package.sources_count; i++) {
			toml_datum_t src      = toml_string_at(sources_arr, (i64)i);
			m->package.sources[i] = toml_datum_to_string(src);
		}
	}

	toml_array_t *headers_arr = toml_array_in(conf, "headers");
	if (headers_arr == nullptr && pkg != nullptr) {
		headers_arr = toml_array_in(pkg, "headers");
	}
	if (headers_arr) {
		m->package.headers_count = (size_t)toml_array_nelem(headers_arr);
		m->package.headers       = calloc(m->package.headers_count, sizeof(sds));
		if (m->package.headers == nullptr && m->package.headers_count > 0) {
			manifest_free(m);
			toml_free(conf);
			return nullptr;
		}
		for (size_t i = 0; i < m->package.headers_count; i++) {
			toml_datum_t hdr      = toml_string_at(headers_arr, (i64)i);
			m->package.headers[i] = toml_datum_to_string(hdr);
		}
	}

	toml_table_t *features_table = toml_table_in(conf, "features");
	if (features_table) {
		m->features_count = 0;
		for (i64 i = 0;; i++) {
			const char *key = toml_key_in(features_table, i);
			if (key == nullptr) {
				break;
			}
			toml_array_t *arr = toml_array_in(features_table, key);
			if (arr == nullptr) {
				continue;
			}
			m->features_count++;
		}

		if (m->features_count > 0) {
			m->features = calloc(m->features_count, sizeof(feature_def_t));
			if (m->features == nullptr) {
				manifest_free(m);
				toml_free(conf);
				return nullptr;
			}
			size_t idx = 0;
			for (i64 i = 0;; i++) {
				const char *key = toml_key_in(features_table, i);
				if (key == nullptr) {
					break;
				}
				toml_array_t *arr = toml_array_in(features_table, key);
				if (arr == nullptr) {
					continue;
				}
				m->features[idx].name = sdsnew(key);
				if (!is_valid_feature_name(m->features[idx].name)) {
					fprintf_safe(stderr, "Warning: Invalid feature name: %s\n", key);
				}
				m->features[idx].deps_count = (size_t)toml_array_nelem(arr);
				if (m->features[idx].deps_count > 0) {
					m->features[idx].deps = calloc(m->features[idx].deps_count, sizeof(sds));
					if (m->features[idx].deps == nullptr) {
						manifest_free(m);
						toml_free(conf);
						return nullptr;
					}
					for (size_t j = 0; j < m->features[idx].deps_count; j++) {
						toml_datum_t dep         = toml_string_at(arr, (i64)j);
						m->features[idx].deps[j] = toml_datum_to_string(dep);
					}
				}
				idx++;
			}
		}
	}

	for (size_t i = 0; i < m->features_count; i++) {
		for (size_t j = 0; j < m->features[i].deps_count; j++) {
			sds dep = m->features[i].deps[j];
			if (dep == nullptr) {
				continue;
			}
			for (size_t k = 0; k < m->features_count; k++) {
				if (k == i) {
					continue;
				}
				if (m->features[k].name != nullptr && sdscmp(dep, m->features[k].name) == 0) {
					for (size_t l = 0; l < m->features[k].deps_count; l++) {
						if (m->features[k].deps[l] != nullptr &&
						    sdscmp(m->features[k].deps[l], m->features[i].name) == 0) {
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

	/* Parse [[bin]] array of tables */
	toml_array_t *bin_arr = toml_array_in(conf, "bin");
	if (bin_arr) {
		m->bin_count = (size_t)toml_array_nelem(bin_arr);
		if (m->bin_count > 0) {
			m->bin = calloc(m->bin_count, sizeof(binary_target_t));
			if (m->bin == nullptr) {
				manifest_free(m);
				toml_free(conf);
				return nullptr;
			}
			for (size_t i = 0; i < m->bin_count; i++) {
				toml_table_t *bt = toml_table_at(bin_arr, (i64)i);
				if (bt == nullptr) {
					continue;
				}
				toml_datum_t bname    = toml_string_in(bt, "name");
				m->bin[i].name        = toml_datum_to_string(bname);
				toml_array_t *src_arr = toml_array_in(bt, "src");
				if (src_arr) {
					m->bin[i].src_count = (size_t)toml_array_nelem(src_arr);
					if (m->bin[i].src_count > 0) {
						m->bin[i].src = calloc(m->bin[i].src_count, sizeof(sds));
						if (m->bin[i].src == nullptr) {
							manifest_free(m);
							toml_free(conf);
							return nullptr;
						}
						for (size_t j = 0; j < m->bin[i].src_count; j++) {
							toml_datum_t src = toml_string_at(src_arr, (i64)j);
							m->bin[i].src[j] = toml_datum_to_string(src);
						}
					}
				}
			}
		}
	}

	toml_free(conf);
	return m;
}

void manifest_extract_dep_info(const char *entry, sds *name_out, sds *version_out)
{
	if (entry == nullptr || !name_out || !version_out) {
		if (name_out) {
			*name_out = nullptr;
		}
		if (version_out) {
			*version_out = nullptr;
		}
		return;
	}

	*name_out    = nullptr;
	*version_out = nullptr;

	const char *eq = strchr(entry, '=');
	if (eq == nullptr) {
		const char *end = entry + strlen(entry);
		while (end > entry && (*(end - 1) == ' ' || *(end - 1) == '\t')) {
			end--;
		}
		size_t len   = (size_t)(end - entry);
		*name_out    = sdsnewlen(entry, len);
		*version_out = sdsnew("*");
		return;
	}

	const char *name_end = eq - 1;
	while (name_end > entry && (*name_end == ' ' || *name_end == '\t')) {
		name_end--;
	}
	size_t name_len = (size_t)(name_end - entry + 1);
	*name_out       = sdsnewlen(entry, name_len);

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
	*version_out   = sdsnewlen(ver_start, ver_len);
}

void manifest_free(manifest_t *m)
{
	if (m == nullptr) {
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

	for (size_t i = 0; i < m->bin_count; i++) {
		free_binary(&m->bin[i]);
	}
	free(m->bin);

	free(m);
}

i64 manifest_write(sds path, manifest_t *m)
{
	FILE *fp = fopen(path, "w");
	if (fp == nullptr) {
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
		fprintf_safe(fp, "headers = [\n");
		for (size_t i = 0; i < m->package.headers_count; i++) {
			if (m->package.headers[i]) {
				fprintf_safe(fp, "  \"%s\",\n", m->package.headers[i]);
			}
		}
		fprintf_safe(fp, "]\n");
	}

	if (m->package.dependencies_count > 0) {
		fprintf_safe(fp, "\n[dependencies]\n");
		for (size_t i = 0; i < m->package.dependencies_count; i++) {
			if (m->package.dependencies[i]) {
				fprintf_safe(fp, "%s\n", m->package.dependencies[i]);
			}
		}
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

	if (m->bin_count > 0) {
		fprintf_safe(fp, "\n");
		for (size_t i = 0; i < m->bin_count; i++) {
			if (m->bin[i].name) {
				fprintf_safe(fp, "[[bin]]\n");
				fprintf_safe(fp, "name = \"%s\"\n", m->bin[i].name);
				if (m->bin[i].src_count > 0) {
					fprintf_safe(fp, "src = [\n");
					for (size_t j = 0; j < m->bin[i].src_count; j++) {
						if (m->bin[i].src[j]) {
							fprintf_safe(fp, "  \"%s\",\n", m->bin[i].src[j]);
						}
					}
					fprintf_safe(fp, "]\n");
				}
				fprintf_safe(fp, "\n");
			}
		}
	}

	fclose(fp);
	return 0;
}
