#include "version.h"

#include "safe.h"

#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Public API — version parsing and comparison                         */
/* ------------------------------------------------------------------ */

bool version_parse(const char *s, version_t *out)
{
	if (s == nullptr || out == nullptr) {
		return false;
	}

	out->major = 0;
	out->minor = 0;
	out->patch = 0;

	const char *start = s;
	char       *end   = nullptr;

	/* Parse major */
	while (isspace(*start)) {
		start++;
	}
	if (*start == '\0' || !isdigit(*start)) {
		return false;
	}
	unsigned long long v = strtoull(start, &end, 10);
	if (end == start || v > UINT64_MAX) {
		return false;
	}
	out->major = (uint64_t)v;
	if (*end != '.') {
		return true;
	}

	/* Parse minor */
	start = end + 1;
	while (isspace(*start)) {
		start++;
	}
	if (*start == '\0' || !isdigit(*start)) {
		return false;
	}
	v          = strtoull(start, &end, 10);
	out->minor = (uint64_t)v;
	if (*end != '.') {
		return true;
	}

	/* Parse patch */
	start = end + 1;
	while (isspace(*start)) {
		start++;
	}
	if (*start == '\0' || !isdigit(*start)) {
		return false;
	}
	v          = strtoull(start, &end, 10);
	out->patch = (uint64_t)v;
	return true;
}

/*
 * Compare two versions: -1 if a < b, 0 if a == b, 1 if a > b.
 */
int64_t version_cmp(const version_t *a, const version_t *b)
{
	if (a->major != b->major) {
		return a->major < b->major ? -1 : 1;
	}
	if (a->minor != b->minor) {
		return a->minor < b->minor ? -1 : 1;
	}
	if (a->patch != b->patch) {
		return a->patch < b->patch ? -1 : 1;
	}
	return 0;
}

/*
 * Extract operator from a constraint string segment.
 * Returns the number of characters consumed (0 if no operator found).
 * Sets *op to: 1=>=, 2=<=, 3=>, 4=<, 5===, 6=!=
 */
static int64_t extract_op(const char *s, int64_t *op)
{
	if (s[0] == '>' && s[1] == '=') {
		*op = 1;
		return 2;
	}
	if (s[0] == '<' && s[1] == '=') {
		*op = 2;
		return 2;
	}
	if (s[0] == '=' && s[1] == '=') {
		*op = 5;
		return 2;
	}
	if (s[0] == '!' && s[1] == '=') {
		*op = 6;
		return 2;
	}
	if (s[0] == '>') {
		*op = 3;
		return 1;
	}
	if (s[0] == '<') {
		*op = 4;
		return 1;
	}
	if (s[0] == '=') {
		*op = 5;
		return 1;
	}
	*op = 0;
	return 0;
}

/*
 * Check a single constraint (operator + version) against an actual version.
 * constraint_segment: e.g. ">= 1.0.0" or "== 2.0" or "*"
 */
static bool check_single(const version_t *actual, const char *segment)
{
	/* Trim leading whitespace */
	while (isspace(*segment)) {
		segment++;
	}
	if (*segment == '\0') {
		return true;
	}

	/* Wildcard */
	if (*segment == '*') {
		return true;
	}

	/* Extract operator */
	int64_t op     = 0;
	int64_t op_len = extract_op(segment, &op);
	if (op == 0) {
		/* No operator — treat as exact version (==) */
		op_len = 0;
		op     = 5;
	}

	const char *ver_str = segment + op_len;
	while (isspace(*ver_str)) {
		ver_str++;
	}
	if (*ver_str == '\0') {
		return true;
	}

	version_t constraint_ver;
	if (!version_parse(ver_str, &constraint_ver)) {
		/* Unparseable version in constraint — warn and pass */
		fprintf_safe(stderr, "Warning: unparseable version in constraint '%s'\n", segment);
		return true;
	}

	int64_t cmp = version_cmp(actual, &constraint_ver);
	switch (op) {
	case 1:
		return cmp >= 0; /* >= */
	case 2:
		return cmp <= 0; /* <= */
	case 3:
		return cmp > 0; /* > */
	case 4:
		return cmp < 0; /* < */
	case 5:
		return cmp == 0; /* == */
	case 6:
		return cmp != 0; /* != */
	default:
		return true;
	}
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool version_satisfies(const char *actual, const char *constraint)
{
	if (actual == nullptr || constraint == nullptr) {
		return true;
	}

	/* Empty constraint or wildcard */
	if (constraint[0] == '\0' || constraint[0] == '*') {
		return true;
	}

	/* Actual version is "*" (no version specified) — always satisfied */
	if (actual[0] == '*' && actual[1] == '\0') {
		return true;
	}

	/* Parse the actual version */
	version_t actual_ver;
	if (!version_parse(actual, &actual_ver)) {
		fprintf_safe(stderr, "Warning: unparseable version '%s'\n", actual);
		return true;
	}

	/* Walk comma-separated constraints */
	const char *cursor = constraint;
	while (*cursor != '\0') {
		/* Extract one segment before the next comma */
		const char *seg_start = cursor;
		const char *comma     = strchr(cursor, ',');
		size_t      seg_len;
		if (comma != nullptr) {
			seg_len = (size_t)(comma - cursor);
		} else {
			seg_len = strlen(cursor);
		}

		/* Allocate a temporary buffer for this segment */
		char *seg = safe_calloc(seg_len + 1, 1);
		if (seg == nullptr) {
			return true;
		}
		memcpy(seg, seg_start, seg_len);
		seg[seg_len] = '\0';

		bool ok = check_single(&actual_ver, seg);
		safe_free(seg);

		if (!ok) {
			return false;
		}

		if (comma != nullptr) {
			cursor = comma + 1;
		} else {
			break;
		}
	}

	return true;
}
