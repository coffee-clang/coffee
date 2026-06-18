#ifndef SKELETON_H_
#define SKELETON_H_

#include "skeletons.h"

#include <string.h>

#include <ctype.h>

static sds skeleton_substitute(const char *tmpl, const char *name)
{
	sds         result = sdsempty();
	const char *p      = tmpl;

	sds name_upper = sdsnew(name);
	for (size_t i = 0; i < sdslen(name_upper); i++) {
		name_upper[i] = (char)toupper((unsigned char)name_upper[i]);
	}

	while (*p) {
		if (strncmp(p, "__NAME_UPPER__", 14) == 0) {
			result = sdscatsds(result, name_upper);
			p += 14;
		} else if (strncmp(p, "__NAME__", 8) == 0) {
			result = sdscat(result, name);
			p += 8;
		} else {
			result = sdscatlen(result, p, 1);
			p++;
		}
	}

	sdsfree(name_upper);
	return result;
}

#endif // SKELETON_H_
