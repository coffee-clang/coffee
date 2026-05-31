#ifndef PROJECT_H_
#define PROJECT_H_

#include "manifest.h"

sds         project_find_manifest(sds start_dir);
manifest_t *project_load_manifest(sds path);
sds         project_get_name(manifest_t *m);

#endif
