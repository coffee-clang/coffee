#include "../coffee.h"
#include "../registry.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <toml.h>
#include <unistd.h>

/*
 * Split "section.key" into section and key parts.
 * Returns pointers into the original string (no allocation).
 */
static void split_key(const char *dotkey, sds *section, const char **key)
{
	const char *dot = strchr(dotkey, '.');
	if (dot) {
		*section = sdsnewlen(dotkey, (size_t)(dot - dotkey));
		*key     = dot + 1;
	} else {
		*section = nullptr;
		*key     = dotkey;
	}
}

/*
 * Build config file path: ~/.coffee/config.toml
 */
static sds config_path(void)
{
	return sdscatprintf(sdsempty(), "%s/config.toml", coffee_home_dir());
}

/*
 * Read a TOML value for a section/key pair.
 * Returns sds string (caller frees) or nullptr if not found.
 */
static sds config_read_raw(const char *section, const char *key)
{
	sds   path = config_path();
	FILE *fp   = fopen(path, "r");
	sds   ret  = nullptr;
	sdsfree(path);

	if (fp == nullptr) {
		return nullptr;
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		return nullptr;
	}

	toml_table_t *tab = conf;
	if (section != nullptr) {
		tab = toml_table_in(conf, section);
	}

	if (tab) {
		toml_raw_t raw = toml_raw_in(tab, key);
		if (raw) {
			char *s;
			if (toml_rtos(raw, &s) == 0 && s) {
				ret = sdsnew(s);
				free(s);
			} else {
				/* Try as integer */
				int64_t ival;
				if (toml_rtoi(raw, &ival) == 0) {
					ret = sdscatprintf(sdsempty(), "%" PRId64, ival);
				}
			}
		}
	}

	toml_free(conf);
	return ret;
}

/*
 * Print all config values in the file.
 */
static i64 config_list(void)
{
	sds   path = config_path();
	FILE *fp   = fopen(path, "r");
	sdsfree(path);

	if (fp == nullptr) {
		printf_safe("(no config file)\n");
		return 0;
	}

	char          errbuf[256];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (conf == nullptr) {
		printf_safe("(empty config)\n");
		return 0;
	}

	/* Top-level keys */
	i64 n = toml_table_nkval(conf);
	for (i64 i = 0; i < n; i++) {
		const char *k   = toml_key_in(conf, i);
		toml_raw_t  raw = toml_raw_in(conf, k);
		if (raw) {
			char *s;
			if (toml_rtos(raw, &s) == 0 && s) {
				printf_safe("%s = \"%s\"\n", k, s);
				free(s);
			}
		}
	}

	/* Tables */
	i64 nt = toml_table_ntab(conf);
	for (i64 i = 0; i < nt; i++) {
		const char   *tabname = toml_key_in(conf, i);
		toml_table_t *tab     = toml_table_in(conf, tabname);
		if (tab == nullptr) {
			continue;
		}
		i64 nk = toml_table_nkval(tab);
		for (i64 j = 0; j < nk; j++) {
			const char *k   = toml_key_in(tab, j);
			toml_raw_t  raw = toml_raw_in(tab, k);
			if (raw) {
				char *s;
				if (toml_rtos(raw, &s) == 0 && s) {
					printf_safe("%s.%s = \"%s\"\n", tabname, k, s);
					free(s);
				}
			}
		}
	}

	toml_free(conf);
	return 0;
}

/*
 * Set a key=value in the config file.
 * Reads file, updates matching line, appends if not found.
 */
static i64 config_set_raw(const char *section, const char *key, const char *value)
{
	sds   path  = config_path();
	FILE *fp    = fopen(path, "r");
	bool  found = false;

	/* Read all lines */
	sds   *lines  = nullptr;
	size_t nlines = 0;
	size_t cap    = 0;

	if (fp) {
		char buf[4096];
		while (fgets(buf, (int)sizeof(buf), fp)) {
			if (nlines >= cap) {
				cap   = cap ? cap * 2 : 64;
				lines = (sds *)realloc(lines, sizeof(sds) * cap);
			}
			lines[nlines] = sdsnew(buf);
			nlines++;
		}
		fclose(fp);
	}

	/* Ensure parent directory exists */
	sds   dir        = sdsnew(path);
	char *last_slash = strrchr(dir, '/');
	if (last_slash) {
		*last_slash = '\0';
		sds mkcmd   = sdscatprintf(sdsempty(), "mkdir -p %s", dir);
		system(mkcmd);
		sdsfree(mkcmd);
	}
	sdsfree(dir);

	/* Build the new key=value line */
	sds new_line = sdscatprintf(sdsempty(), "%s = \"%s\"\n", key, value);

	/* Write back */
	fp = fopen(path, "w");
	if (fp == nullptr) {
		fprintf_safe(stderr, "Error: Could not write %s\n", path);
		sdsfree(path);
		sdsfree(new_line);
		for (size_t i = 0; i < nlines; i++) {
			sdsfree(lines[i]);
		}
		free(lines);
		return 1;
	}

	sds sect_header = nullptr;
	if (section) {
		sect_header = sdscatprintf(sdsempty(), "[%s]", section);
	}

	/* Write back, tracking which section we're in */
	bool in_target_section = false;
	bool section_written   = false;

	for (size_t i = 0; i < nlines; i++) {
		sds trimmed = sdsnew(lines[i]);
		sdstrim(trimmed, " \t\r\n");

		/* Check if this line is a section header */
		if (trimmed[0] == '[') {
			/* Leaving target section? If section exists but key wasn't found, insert it */
			if (in_target_section && !found && section) {
				fprintf_safe(fp, "%s", new_line);
				found = true;
			}
			in_target_section = false;

			/* Entering target section? */
			if (section && sdscmp(trimmed, sect_header) == 0) {
				in_target_section = true;
				section_written   = true;
			}
		}

		/* Within target section, check for matching key */
		bool match = false;
		if (in_target_section) {
			/* Skip comment/empty lines when looking for key */
			if (strncmp(trimmed, key, strlen(key)) == 0 &&
			    (trimmed[strlen(key)] == '=' || trimmed[strlen(key)] == ' ' || trimmed[strlen(key)] == '\0')) {
				match = true;
			}
		}

		if (match) {
			found = true;
			fprintf_safe(fp, "%s", new_line);
		} else {
			fprintf_safe(fp, "%s", lines[i]);
		}
		sdsfree(trimmed);
	}

	/* If section exists but key was never found (insert at end of section) */
	if (in_target_section && !found && section) {
		fprintf_safe(fp, "%s", new_line);
		found = true;
	}

	/* If section doesn't exist at all, add it */
	if (section && !section_written) {
		fprintf_safe(fp, "\n%s\n%s", sect_header, new_line);
		found = true;
	}

	/* No section, key not found — append at end */
	if (!found) {
		fprintf_safe(fp, "%s", new_line);
	}

	sdsfree(sect_header);

	fclose(fp);

	sdsfree(path);
	sdsfree(new_line);
	for (size_t i = 0; i < nlines; i++) {
		sdsfree(lines[i]);
	}
	free(lines);
	return 0;
}

/*
 * Unset a key from config file (remove the line).
 */
static i64 config_unset_raw(const char *section, const char *key)
{
	(void)section;
	sds   path = config_path();
	FILE *fp   = fopen(path, "r");

	if (fp == nullptr) {
		return 0;
	}

	/* Read all lines */
	sds   *lines  = nullptr;
	size_t nlines = 0;
	size_t cap    = 0;
	char   buf[4096];

	while (fgets(buf, (int)sizeof(buf), fp)) {
		if (nlines >= cap) {
			cap   = cap ? cap * 2 : 64;
			lines = (sds *)realloc(lines, sizeof(sds) * cap);
		}
		lines[nlines] = sdsnew(buf);
		nlines++;
	}
	fclose(fp);

	/* Write back, skipping the matching line */
	fp = fopen(path, "w");
	if (fp == nullptr) {
		sdsfree(path);
		for (size_t i = 0; i < nlines; i++) {
			sdsfree(lines[i]);
		}
		free(lines);
		return 1;
	}

	for (size_t i = 0; i < nlines; i++) {
		sds trimmed = sdsnew(lines[i]);
		sdstrim(trimmed, " \t\r\n");

		bool skip = false;
		if (strncmp(trimmed, key, strlen(key)) == 0 && (trimmed[strlen(key)] == '=' || trimmed[strlen(key)] == ' ')) {
			skip = true;
		}

		sdsfree(trimmed);

		if (!skip) {
			fprintf_safe(fp, "%s", lines[i]);
		}
	}

	fclose(fp);

	sdsfree(path);
	for (size_t i = 0; i < nlines; i++) {
		sdsfree(lines[i]);
	}
	free(lines);
	return 0;
}

int64_t handle_config(options *opts)
{
	if (opts->inputs_num <= 1) {
		config_list();
		return 0;
	}

	const char *sub = opts->inputs[1];

	if (strcmp(sub, "list") == 0 || strcmp(sub, "--list") == 0) {
		return config_list();
	}

	if (strcmp(sub, "get") == 0) {
		if (opts->inputs_num < 3) {
			fprintf_safe(stderr, "Usage: coffee config get <key>\n");
			return 1;
		}
		const char *key = opts->inputs[2];
		sds         section;
		const char *k;
		split_key(key, &section, &k);

		sds val = config_read_raw(section, k);
		if (val == nullptr) {
			fprintf_safe(stderr, "Error: key '%s' not found\n", key);
			sdsfree(section);
			return 1;
		}
		printf_safe("%s\n", val);
		sdsfree(val);
		sdsfree(section);
		return 0;
	}

	if (strcmp(sub, "set") == 0) {
		if (opts->inputs_num < 4) {
			fprintf_safe(stderr, "Usage: coffee config set <key> <value>\n");
			return 1;
		}
		const char *key   = opts->inputs[2];
		const char *value = opts->inputs[3];
		sds         section;
		const char *k;
		split_key(key, &section, &k);
		i64 ret = config_set_raw(section, k, value);
		sdsfree(section);
		return ret;
	}

	if (strcmp(sub, "unset") == 0) {
		if (opts->inputs_num < 3) {
			fprintf_safe(stderr, "Usage: coffee config unset <key>\n");
			return 1;
		}
		const char *key = opts->inputs[2];
		sds         section;
		const char *k;
		split_key(key, &section, &k);
		i64 ret = config_unset_raw(section, k);
		sdsfree(section);
		return ret;
	}

	fprintf_safe(stderr, "Error: Unknown config subcommand '%s'. Supported: --list, get, set, unset\n", sub);
	return 1;
}
