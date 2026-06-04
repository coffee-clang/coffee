#ifndef PROJECT_H_
#define PROJECT_H_

#include "manifest.h"

sds         project_find_manifest(sds start_dir);
manifest_t *project_load_manifest(sds path);
sds         project_get_name(manifest_t *m);
sds         pkg_dir(const char *name);
i64         create_dir(const char *path);
i64         create_file(const char *path, const char *content);
sds         resolve_dep_version(const char *dep_dir);

#endif
