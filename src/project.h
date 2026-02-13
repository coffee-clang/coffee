#ifndef PROJECT_H_
#define PROJECT_H_

#include "manifest.h"

char *project_find_manifest(const char *start_dir);
manifest_t *project_load_manifest(const char *path);
char *project_get_name(manifest_t *m);

#endif
