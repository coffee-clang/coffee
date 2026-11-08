#include "../build.h"
#include "../coffee.h"
#include "../registry.h"
#include "safe.h"

#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <toml.h>
#include <unistd.h>

/*
 * Default config values. Changing this list updates what install-update-config
 * bootstraps. Keys with a section are written under [section]; values are
 * always strings.
 */
struct default_entry {
	const char *section;
	const char *key;
	const char *value;
};

static const struct default_entry defaults[] = {
	{ nullptr, "default-target", "debug" },
	{ nullptr, "default-edition", "c23" },
	{ "build", "jobs", "0" },
	{ "registry", "index-url", "https://coffee-clang.github.io/recipes/.well-known/packages.json.zstd" },
};

static sds cfg_path(void)
{
	return sdscatprintf(sdsempty(), "%s/config.toml", coffee_home_dir());
}

/*
 * Read all raw lines from config file into an sds array.
 * Returns number of lines read (0 if file does not exist).
 */
static size_t read_config_lines(const char *path, sds **lines_out)
{
	FILE *fp = safe_fopen(path, "r");
	if (fp == nullptr) {
		*lines_out = nullptr;
		return 0;
	}

	sds   *lines  = nullptr;
	size_t nlines = 0;
	size_t cap    = 0;
	char   buf[4'096];

	while (fgets(buf, (int)sizeof(buf), fp)) {
		if (nlines >= cap) {
			cap   = cap ? cap * 2 : 64;
			lines = (sds *)safe_realloc(lines, sizeof(sds) * cap);
		}
		lines[nlines] = sdsnew(buf);
		nlines++;
	}
	safe_fclose(fp);

	*lines_out = lines;
	return nlines;
}

/*
 * Check if a given section/key exists in the parsed config.
 */
static bool key_exists(toml_table_t *conf, const char *section, const char *key)
{
	toml_table_t *tab = conf;
	if (section != nullptr) {
		tab = toml_table_in(conf, section);
	}
	if (tab == nullptr) {
		return false;
	}
	return toml_raw_in(tab, key) != nullptr;
}

/*
 * Check which section headers are present in lines.
 */
static bool section_exists_in_lines(sds *lines, size_t nlines, const char *section)
{
	sds header         = sdscatprintf(sdsempty(), "[%s]", section);
	sds trimmed_header = sdsnew(header);
	sdstrim(trimmed_header, " \t\r\n");

	for (size_t i = 0; i < nlines; i++) {
		sds trimmed = sdsnew(lines[i]);
		sdstrim(trimmed, " \t\r\n");
		if (sdscmp(trimmed, trimmed_header) == 0) {
			sdsfree(trimmed);
			sdsfree(trimmed_header);
			sdsfree(header);
			return true;
		}
		sdsfree(trimmed);
	}

	sdsfree(trimmed_header);
	sdsfree(header);
	return false;
}

/*
 * Net number of array brackets opened by a raw config line, ignoring
 * quoted strings and comments.
 */
static i64 bracket_delta(const char *line)
{
	i64  delta = 0;
	char quote = '\0';

	for (const char *p = line; *p != '\0'; p++) {
		if (quote != '\0') {
			if (*p == quote) {
				quote = '\0';
			}
		} else if (*p == '"' || *p == '\'') {
			quote = *p;
		} else if (*p == '#') {
			break;
		} else if (*p == '[') {
			delta++;
		} else if (*p == ']') {
			delta--;
		}
	}
	return delta;
}

/*
 * Index of the first [section] header at or after `from`, or nlines when
 * there is none.  `from` must be outside any multi-line array.  A line
 * starting with '[' inside a multi-line array is a nested array, not a header.
 */
static size_t next_header_index(sds *lines, size_t nlines, size_t from)
{
	i64 depth = 0;

	for (size_t i = from; i < nlines; i++) {
		const char *p = lines[i];
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (depth == 0 && *p == '[') {
			return i;
		}
		depth += bracket_delta(p);
		if (depth < 0) {
			depth = 0;
		}
	}
	return nlines;
}

/*
 * Insert line at position insert_at, shifting the following lines down.
 */
static void insert_line(sds **lines_out, size_t *nlines, size_t *cap, size_t insert_at, sds line)
{
	if (*nlines >= *cap) {
		*cap       = *cap ? *cap * 2 : 64;
		*lines_out = (sds *)safe_realloc(*lines_out, sizeof(sds) * *cap);
	}
	/* A last line without a newline would swallow the inserted one */
	if (insert_at > 0 && insert_at == *nlines) {
		sds prev = (*lines_out)[insert_at - 1];
		if (sdslen(prev) > 0 && prev[sdslen(prev) - 1] != '\n') {
			(*lines_out)[insert_at - 1] = sdscat(prev, "\n");
		}
	}
	for (size_t i = *nlines; i > insert_at; i--) {
		(*lines_out)[i] = (*lines_out)[i - 1];
	}
	(*lines_out)[insert_at] = line;
	(*nlines)++;
}

/*
 * Append a key=value line inside [section] to the lines array.
 * Adds the section header if it doesn't exist yet.
 */
static void append_key(sds **lines_out, size_t *nlines, size_t *cap, const char *section, const char *key,
                       const char *value)
{
	sds new_line = sdscatprintf(sdsempty(), "%s = \"%s\"\n", key, value);

	if (section == nullptr) {
		/* Top-level key: must precede the first section header, otherwise
		 * TOML would place it inside that section. */
		size_t insert_at = next_header_index(*lines_out, *nlines, 0);
		insert_line(lines_out, nlines, cap, insert_at, new_line);
		return;
	}

	/* Find or create section */
	if (!section_exists_in_lines(*lines_out, *nlines, section)) {
		/* Append section header + key */
		sds header = sdscatprintf(sdsempty(), "\n[%s]\n", section);

		if (*nlines >= *cap) {
			*cap       = *cap ? *cap * 2 : 64;
			*lines_out = (sds *)safe_realloc(*lines_out, sizeof(sds) * *cap);
		}
		(*lines_out)[*nlines] = header;
		(*nlines)++;

		if (*nlines >= *cap) {
			*cap       = *cap ? *cap * 2 : 64;
			*lines_out = (sds *)safe_realloc(*lines_out, sizeof(sds) * *cap);
		}
		(*lines_out)[*nlines] = new_line;
		(*nlines)++;
		return;
	}

	/* Section exists, insert key after section header */
	sds    target_header = sdscatprintf(sdsempty(), "[%s]", section);
	size_t insert_at     = *nlines;

	for (size_t h = next_header_index(*lines_out, *nlines, 0); h < *nlines;
	     h        = next_header_index(*lines_out, *nlines, h + 1)) {
		sds trimmed = sdsnew((*lines_out)[h]);
		sdstrim(trimmed, " \t\r\n");
		bool is_target = sdscmp(trimmed, target_header) == 0;
		sdsfree(trimmed);
		if (is_target) {
			/* Insert before the next header, or at the end */
			insert_at = next_header_index(*lines_out, *nlines, h + 1);
			break;
		}
	}

	insert_line(lines_out, nlines, cap, insert_at, new_line);
	sdsfree(target_header);
}

int64_t handle_install_update_config(options *opts)
{
	(void)opts;

	sds   home   = sdsnew(coffee_home_dir());
	char *argv[] = { "mkdir", "-p", home, nullptr };
	run_command(argv, 0);

	sdsfree(home);

	sds    path   = cfg_path();
	size_t nlines = 0;
	sds   *lines  = nullptr;

	nlines = read_config_lines(path, &lines);

	/* Parse existing config to check which keys are missing */
	toml_table_t *conf = nullptr;
	if (nlines > 0) {
		FILE *fp = safe_fopen(path, "r");
		if (fp) {
			char errbuf[256];
			conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
			safe_fclose(fp);
		}
	}

	size_t cap = nlines > 0 ? nlines : 64;
	if (nlines == 0) {
		lines = (sds *)safe_malloc(sizeof(sds) * cap);
	}
	size_t added = 0;
	size_t ndefs = sizeof(defaults) / sizeof(defaults[0]);

	for (size_t i = 0; i < ndefs; i++) {
		const char *sec = defaults[i].section;
		const char *key = defaults[i].key;
		const char *val = defaults[i].value;

		bool exists = false;
		if (conf != nullptr) {
			exists = key_exists(conf, sec, key);
		}
		if (exists) {
			continue;
		}

		append_key(&lines, &nlines, &cap, sec, key, val);
		added++;
	}

	if (conf != nullptr) {
		toml_free(conf);
	}

	if (added == 0) {
		printf_safe("Configuration is up-to-date.\n");
		sdsfree(path);
		for (size_t i = 0; i < nlines; i++) {
			sdsfree(lines[i]);
		}
		safe_free(lines);
		return 0;
	}

	/* Write back */
	FILE *fp = safe_fopen(path, "w");
	if (fp == nullptr) {
		fprintf_safe(stderr, "Error: Could not write %s\n", path);
		sdsfree(path);
		for (size_t i = 0; i < nlines; i++) {
			sdsfree(lines[i]);
		}
		safe_free(lines);
		return 1;
	}

	for (size_t i = 0; i < nlines; i++) {
		fprintf_safe(fp, "%s", lines[i]);
	}
	safe_fclose(fp);

	printf_safe("Configuration installed/updated: %zu key(s) added.\n", added);

	sdsfree(path);
	for (size_t i = 0; i < nlines; i++) {
		sdsfree(lines[i]);
	}
	safe_free(lines);
	return 0;
}
